#!/usr/bin/perl
use strict;
use warnings;

sub _gib_duchschnitt {
    my ($liste) = @_;
    #return (sort(@$liste))[int(scalar(@$liste) / 2)];
    return 0 if(!scalar($#$liste));

    my $x = 0;
    foreach(@$liste) {
        $x += $_;
    }
    return sprintf("%d", $x / scalar($#$liste));
}
sub _hole_daten {
    my $daten = [];
    my @files = ();
    opendir(my $dh, '../sd-karteninhalt') or die $!;
    while(my $filename = readdir($dh)) {
        next if($filename !~ m/^anlage_log\-\d{4}\-\d{2}\.csv$/);

        push(@files, $filename);
    }
    closedir($dh);
    if(!scalar(@files)) {
        die "Bitte erst 'download_der_daten_der_zentrale.pl [IP]' ausfuehren\n";
    }

    my $fehler = 0;
    foreach my $filename (sort(@files)) {
        my $fh;
        if(!open($fh, '<', "../sd-karteninhalt/$filename")) {
            die "Fehler: $!\n";
        }
        $/ = "\n";
        while(my $line = <$fh>) {
            chomp($line);
            my @e = $line =~ m/^(\d{10,}),e[2],([\-\d]+),([\-\d]+),(\d+),(\d+),(\d+),([\-\d]+),([\-\d]+),([\-\d]+),(\d+),(\d+),/;
            if(!scalar(@e)) {
                # warn $line;
                $fehler++;
                next;
            }

            my @w = $line =~ m/,w[2],(\d+),(\d+)/;
            if(!scalar(@w)) {
                # warn $line;
                $fehler++;
                next;
            }

            my @t = $line =~ m/,k1([\-\d\.]+),([\d\.]+)/;

            my @d = gmtime($e[0]);
            my $neu = {
                zeitpunkt                       => $e[0],
                datum                           => sprintf("%04d-%02d-%02d", $d[5] + 1900, $d[4] + 1, $d[3]),
                stunde                          => $d[2],
                monat                           => $d[4] + 1,
                netzbezug_in_w                  => $e[1],
                solarakku_zuschuss_in_w         => $e[2],
                solarerzeugung_in_w             => $e[3],
                stromverbrauch_in_w             => $e[4],
                solarakku_ladestand_in_promille => $e[5],
                l1_strom_ma                     => $e[6],
                l2_strom_ma                     => $e[7],
                l3_strom_ma                     => $e[8],
                gib_anteil_pv1_in_prozent       => $e[9],
                ersatzstrom_ist_aktiv           => $e[10] ? 1 : 0,

                stunden_solarstrahlung          => $w[0],
                tages_solarstrahlung            => $w[1],
                temperatur                      => $t[0],
                feuchtigkeit                    => $t[1],
            };
            push(@$daten, $neu);
        }
        close($fh);
    }
    if($fehler) {
        warn "ACHTUNG: $fehler Fehler!\n";
    }
    return $daten;
}
my $daten = _hole_daten();
$daten = [sort { $a->{zeitpunkt} <=> $b->{zeitpunkt} } @$daten];
print "\nAnzahl der Log-Datensaetze: " . scalar(@$daten) . "\n";
my $logdaten_in_tagen = ($daten->[$#$daten]->{'zeitpunkt'} - $daten->[0]->{'zeitpunkt'}) / 86400;
print "Betrachteter Zeitraum: " . sprintf("%.1f", $logdaten_in_tagen) . " Tage\n";

my $verbrauch = [];
foreach my $e (@$daten) {
    push(@$verbrauch, $e->{stromverbrauch_in_w});
}
print "Grundverbrauch(via Median): " . (sort(@$verbrauch))[int(scalar(@$verbrauch) / 2)] . " W\n";

my $min_i_in_ma = 0;
my $min_i_phase = 1;
my $max_i_in_ma = 0;
my $max_i_phase = 1;
foreach my $e (@$daten) {
    foreach my $i (1..3) {
        if($e->{"l${i}_strom_ma"} > $max_i_in_ma) {
            $max_i_in_ma = $e->{"l${i}_strom_ma"};
            $max_i_phase = $i;
        }
        if($e->{"l${i}_strom_ma"} < $min_i_in_ma) {
            $min_i_in_ma = $e->{"l${i}_strom_ma"};
            $min_i_phase = $i;
        }
    }
}
print "Min. Strom: " . sprintf("%.2f", $min_i_in_ma / 1000) . " A (Phase $min_i_phase)\n";
print "Max. Strom: " . sprintf("%.2f", $max_i_in_ma / 1000) . " A (Phase $max_i_phase)\n";

my $akku_ladezyklen_in_promille = 0;
my $prognostizierte_vollzyklen = 6000;
my $alt = {solarakku_ladestand_in_promille => 0};
foreach my $e (@$daten) {
    my $ladeveraenderung_in_promille = $alt->{solarakku_ladestand_in_promille} - $e->{solarakku_ladestand_in_promille};
    if($ladeveraenderung_in_promille > 0) {
        $akku_ladezyklen_in_promille += $ladeveraenderung_in_promille;
    }
    $alt = $e;
}
print "Vollzyklen des Akkus: " . sprintf("%.2f", $akku_ladezyklen_in_promille / 1000) . "\n";
my $prognose_akku_haltbar_in_tagen = $prognostizierte_vollzyklen / ($akku_ladezyklen_in_promille / 1000) * $logdaten_in_tagen;
print "Haltbarkeit des Akkus(gesamt; bei max. $prognostizierte_vollzyklen Vollzyklen): " . sprintf("%.1f", $prognose_akku_haltbar_in_tagen / 365) . " Jahre\n";

my $strahlungsdaten = {};
foreach my $monat (1..12) {
    $strahlungsdaten->{$monat} ||= {};
    my $tmp_strahlungsdaten = $strahlungsdaten->{$monat};
    foreach my $e (@$daten) {
        next if($e->{monat} != $monat);

        my $key = $e->{stunden_solarstrahlung};
        $tmp_strahlungsdaten->{$key} ||= {
            solarerzeugung_in_w   => [],
            stunden_solarstrahlung => [],
        };
        push(@{$tmp_strahlungsdaten->{$key}->{stunden_solarstrahlung}}, $e->{stunden_solarstrahlung});
        if(
            !$e->{stunden_solarstrahlung}
            || $e->{solarerzeugung_in_w} / $e->{stunden_solarstrahlung} > 100
        ) {
            next;
        }
        push(@{$tmp_strahlungsdaten->{$key}->{solarerzeugung_in_w}}, $e->{solarerzeugung_in_w});
    }
}

# Temperatur!
print "Umrechnungsfaktor Solarstrahlung pro h -> Leistung in W (Monatsweise):\n";
my $globaler_faktor = [];
foreach my $monat (1..12) {
    my $faktor_liste = [];
    my $tmp_strahlungsdaten = $strahlungsdaten->{$monat};
    foreach my $key (sort { int($a) <=> int($b) } keys(%$tmp_strahlungsdaten)) {
        my $e = $tmp_strahlungsdaten->{$key};
        my $stunden_solarstrahlung = _gib_duchschnitt($e->{stunden_solarstrahlung});
        next if(!$stunden_solarstrahlung);

        my $erzeugung_in_w = _gib_duchschnitt($e->{solarerzeugung_in_w});
        next if(!$erzeugung_in_w);

        push(@$faktor_liste, $erzeugung_in_w / $stunden_solarstrahlung * 100);
        push(@$globaler_faktor, $faktor_liste->[$#$faktor_liste]);
    }
    printf(" $monat: %.2f\n", _gib_duchschnitt($faktor_liste) / 100);
}
printf("Gesamt: %.2f\n", _gib_duchschnitt($globaler_faktor) / 100);

my $min_temp = 999;
my $min_time = 0;
my $max_temp = 0;
my $max_time = 0;
my $delta_temp = 0;
my $delta_time = 0;
foreach my $e (@$daten) {
    if($e->{"temperatur"} && $e->{"temperatur"} != 0 && $e->{'zeitpunkt'} > 1684689496) {
        if($e->{'temperatur'} < $max_temp) {
            if(
                $max_temp
                && $min_temp < $max_temp
                && sprintf("%.1f", $max_temp - $min_temp) >= 0.4
            ) {
                $delta_temp += $max_temp - $min_temp;
                $delta_time += ($max_time - $min_time) / 3600;
                printf("Delta: %.1f K in %.2f h $min_temp $max_temp\n", $max_temp - $min_temp, ($max_time - $min_time) / 3600);
            }
            $min_temp = $e->{'temperatur'};
            $min_time = $e->{'zeitpunkt'};
            $max_temp = $e->{'temperatur'};
            $max_time = $e->{'zeitpunkt'};
        } elsif($e->{'temperatur'} > $max_temp) {
            $max_temp = $e->{'temperatur'};
            $max_time = $e->{'zeitpunkt'};
        }
    }
}
printf("Temperaturertrag: %.2f K/h\n", $delta_temp / $delta_time);

print "\n";
