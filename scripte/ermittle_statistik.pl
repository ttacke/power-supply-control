#!/usr/bin/perl
use strict;
use warnings;

sub _gib_duchschnitt {
    my ($liste) = @_;
    #return (sort(@$liste))[int(scalar(@$liste) / 2)];
    return 0 if(!scalar(@$liste));

    my $x = 0;
    foreach(@$liste) {
        $x += $_;
    }
    return sprintf("%.4f", $x / scalar(@$liste));
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
            my @e = $line =~ m/^(\d{10,}),(e[23]),([\-\d]+),([\-\d]+),(\d+),(\d+),(\d+),([\-\d]+),([\-\d]+),([\-\d]+),(\d+),(\d+),(?:(\d+),|)/;
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

            my @t = $line =~ m/,kb?1([\-\d\.]+),([\d\.]+),kl?1([\-\d\.]+),([\d\.]+)/;

            my @d = gmtime($e[0]);
            my $neu = {
                zeitpunkt                       => $e[0],
                datum                           => sprintf("%04d-%02d-%02d", $d[5] + 1900, $d[4] + 1, $d[3]),
                stunde                          => $d[2],
                monat                           => $d[4] + 1,
                netzbezug_in_w                  => $e[2],
                solarakku_zuschuss_in_w         => $e[3],
                solarerzeugung_in_w             => $e[4],
                stromverbrauch_in_w             => $e[5],
                solarakku_ladestand_in_promille => $e[6],
                l1_strom_ma                     => $e[7],
                l2_strom_ma                     => $e[8],
                l3_strom_ma                     => $e[9],
                gib_anteil_pv1_in_prozent       => $e[10],
                ersatzstrom_ist_aktiv           => $e[11] ? 1 : 0,
                gesamt_energiemenge_in_wh       => $e[1] eq 'e2' ? 0 : $e[12],

                stunden_solarstrahlung          => $w[0],
                tages_solarstrahlung            => $w[1],

                erde_temperatur                 => $t[0],
                erde_luftfeuchtigkeit           => $t[1],
                luft_temperatur                 => $t[2],
                luft_luftfeuchtigkeit           => $t[3],
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

foreach my $e (['sommer', [3..9], '800'], ['winter', [10..12,1,2], '1500']) {
    my $name = $e->[0];
    my $monate = $e->[1];
    my $max_grundverbrauch = $e->[2];
    my $verbrauch = [];
    foreach my $e (@$daten) {
        if(
            $e->{stromverbrauch_in_w} < $max_grundverbrauch
            && grep { $_ == $e->{monat} } @$monate
        ) {
            push(@$verbrauch, $e->{stromverbrauch_in_w});
        }
    }
    print "grundverbrauch_in_w_pro_h_$name (via Median): " . (sort(@$verbrauch))[int(scalar(@$verbrauch) / 2)] . " W\n";
}

{
    print "Stromstaerken seit Begrenzung auf 2,99kVA (15.02.2024)\n";
    my $strom = {};
    my $ueberlast_start = 0;
    my $ueberlast_anzahl = 0;
    my $ueberlast_dauer = 0;
    my $ueberlast_max_dauer = 0;
    foreach my $e (@$daten) {
        if($e->{'zeitpunkt'} > 1707994800) {
            if($e->{'netzbezug_in_w'} > 4670) {
                if(!$ueberlast_start) {
                    $ueberlast_start = $e->{'zeitpunkt'};
                    $ueberlast_anzahl++;
                }
            } else {
                if($ueberlast_start) {
                    my $dauer = $e->{'zeitpunkt'} - $ueberlast_start;
                    if($dauer > $ueberlast_max_dauer) {
                        $ueberlast_max_dauer = $dauer;
                    }
                    $ueberlast_dauer += $dauer;
                    $ueberlast_start = 0;
                }
            }

            foreach my $phase (1..3) {
                $strom->{$phase} ||= {
                    min => 0, max => 0,
                    ueberlast_start => 0, ueberlast_anzahl => 0,
                    ueberlast_dauer => 0, ueberlast_max_dauer => 0
                };
                my $i_in_ma = $e->{"l${phase}_strom_ma"};
                if($i_in_ma > 16000) {
                    if(!$strom->{$phase}->{'ueberlast_start'}) {
                        $strom->{$phase}->{'ueberlast_start'} = $e->{'zeitpunkt'};
                        $strom->{$phase}->{'ueberlast_anzahl'}++;
                    }
                } else {
                    if($strom->{$phase}->{'ueberlast_start'}) {
                        my $dauer = $e->{'zeitpunkt'} - $strom->{$phase}->{'ueberlast_start'};
                        if($dauer > $strom->{$phase}->{'ueberlast_max_dauer'}) {
                            $strom->{$phase}->{'ueberlast_max_dauer'} = $dauer;
                        }
                        $strom->{$phase}->{'ueberlast_dauer'} += $dauer;
                        $strom->{$phase}->{'ueberlast_start'} = 0;
                    }
                }
                if($i_in_ma > $strom->{$phase}->{'max'}) {
                    $strom->{$phase}->{'max'} = $i_in_ma;
                }
                if($i_in_ma < $strom->{$phase}->{'min'}) {
                    $strom->{$phase}->{'min'} = $i_in_ma;
                }
            }
        }
    }
    print "Ueberlast: Anzahl = $ueberlast_anzahl, Gesamtdauer = ${ueberlast_dauer}s, max Dauer = ${ueberlast_max_dauer}s\n";
    foreach my $phase (1..3) {
        print "  Phase $phase:\n";
        print "     Min " . sprintf("%.2f", $strom->{$phase}->{'min'} / 1000) . " A\n";
        print "     Max " . sprintf("%.2f", $strom->{$phase}->{'max'} / 1000) . " A\n";
        print "     >16A Anzahl $strom->{$phase}->{'ueberlast_anzahl'}\n";
        print "     >16A Dauer Gesamt $strom->{$phase}->{'ueberlast_dauer'}s\n";
        print "     >16A max.Dauer $strom->{$phase}->{'ueberlast_max_dauer'}\n";
    }
}

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

foreach my $e (['sommer', [3..9]], ['winter', [10..12,1,2]]) {
    my $name = $e->[0];
    my $monate = $e->[1];
    my $strahlungsdaten = {};
    my $zeitraum_daten = {};
    foreach my $monat (@$monate) {
        $strahlungsdaten->{$monat} ||= {};
        my $tmp_strahlungsdaten = $strahlungsdaten->{$monat};
        foreach my $e (@$daten) {
            next if($e->{monat} != $monat);

            next if(!$e->{stunden_solarstrahlung});

            $tmp_strahlungsdaten->{solarerzeugung_in_w} ||= [];
            $tmp_strahlungsdaten->{stunden_solarstrahlung} ||= [];
            push(@{$tmp_strahlungsdaten->{stunden_solarstrahlung}}, $e->{stunden_solarstrahlung});
            push(@{$tmp_strahlungsdaten->{solarerzeugung_in_w}}, $e->{solarerzeugung_in_w});
            push(@{$zeitraum_daten->{stunden_solarstrahlung}}, $e->{stunden_solarstrahlung});
            push(@{$zeitraum_daten->{solarerzeugung_in_w}}, $e->{solarerzeugung_in_w});
        }
    }

    print "Umrechnungsfaktor Solarstrahlung pro h -> Leistung in W (Monatsweise):\n";
    foreach my $monat (@$monate) {
        my $tmp_strahlungsdaten = $strahlungsdaten->{$monat};

        my $stunden_solarstrahlung = _gib_duchschnitt($tmp_strahlungsdaten->{stunden_solarstrahlung});
        next if(!$stunden_solarstrahlung);

        my $erzeugung_in_w = _gib_duchschnitt($tmp_strahlungsdaten->{solarerzeugung_in_w});
        next if(!$erzeugung_in_w);

        my $faktor = $erzeugung_in_w / $stunden_solarstrahlung;
        printf("$monat: %.2f\n", $faktor);
    }
    my $stunden_solarstrahlung = _gib_duchschnitt($zeitraum_daten->{stunden_solarstrahlung});
    my $erzeugung_in_w = _gib_duchschnitt($zeitraum_daten->{solarerzeugung_in_w});
    if($erzeugung_in_w && $stunden_solarstrahlung) {
        my $faktor = $erzeugung_in_w / $stunden_solarstrahlung;
        printf("solarstrahlungs_vorhersage_umrechnungsfaktor_$name: %.2f\n", $faktor * 0.85);
    }
}


my $min_temp = 999;
my $min_time = 0;
my $max_temp = 0;
my $max_time = 0;
my $delta_temp = 0;
my $delta_time = 0;

my $temp_filename = 'temperaturverlauf.csv';
open(my $temp_file, '>', $temp_filename) or die $!;
print $temp_file "Datum,Boden,Luft\n";
foreach my $e (@$daten) {
    if(
        $e->{"erde_temperatur"} && $e->{"erde_temperatur"} != 0
        && $e->{"luft_temperatur"} && $e->{"luft_temperatur"} != 0
    ) {
        my ($sec,$min,$hour,$mday,$mon,$year,$wday,$yday,$isdst) = localtime($e->{'zeitpunkt'});
        if($min % 15 == 0) {
            print $temp_file sprintf(
                "%4d-%02d-%02d %d:%02d,%.1f,%01f\n",
                $year + 1900, $mon + 1, $mday, $hour, $min, $e->{erde_temperatur}, $e->{luft_temperatur}
            );
        }
        if($e->{'erde_temperatur'} < $max_temp) {
            if(
                $max_temp
                && $min_temp < $max_temp
                && sprintf("%.1f", $max_temp - $min_temp) >= 0.4
            ) {
                $delta_temp += $max_temp - $min_temp;
                $delta_time += ($max_time - $min_time) / 3600;
                # printf(
                #     "Delta: %.1f K in %.2f h $min_temp -> $max_temp; %s\n",
                #     $max_temp - $min_temp,
                #     ($max_time - $min_time) / 3600,
                #     '' . localtime($e->{'zeitpunkt'})
                # );
            }
            $min_temp = $e->{'erde_temperatur'};
            $min_time = $e->{'zeitpunkt'};
            $max_temp = $e->{'erde_temperatur'};
            $max_time = $e->{'zeitpunkt'};
        } elsif($e->{'erde_temperatur'} > $max_temp) {
            $max_temp = $e->{'erde_temperatur'};
            $max_time = $e->{'zeitpunkt'};
        }
    }
}
close($temp_file) or die $!;
printf("\nTemperaturertrag: %.2f K/h\n", $delta_temp / $delta_time);
print "CSV-Datei $temp_filename wurde erstellt\n";

my $energiemenge = {};
my $old_key = '';
my $old_gesamtmenge = 0;
print "\nErzeugte Energiemenge (Monatsweise):\n";
foreach my $e (@$daten) {
    next if(!$e->{'gesamt_energiemenge_in_wh'} || $e->{'zeitpunkt'} < 1736857686);

    my ($sec,$min,$hour,$mday,$mon,$year,$wday,$yday,$isdst) = localtime($e->{'zeitpunkt'});
    my $key = sprintf("%02d-%04d", $mon, $year);
    if(!defined($energiemenge->{$key})) {
        if($old_key && $energiemenge->{$old_key} && $old_gesamtmenge) {
            printf("%s: %d kWh", $old_key, $old_gesamtmenge - $energiemenge->{$old_key});
        }
        $energiemenge->{$key} = $e->{'gesamt_energiemenge_in_wh'}
    }
    $old_key = $key;
    $old_gesamtmenge = $e->{'gesamt_energiemenge_in_wh'};
    # warn "$e->{'zeitpunkt'} $e->{'gesamt_energiemenge_in_wh'}";
}
print "\n";
