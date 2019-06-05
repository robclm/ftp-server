#!/bin/bash 
FTP_PATH="../.."

cd example_dir
$FTP_PATH/siftp localhost 1122 < ./commands1.txt &
cd ..

cd example_dir2
$FTP_PATH/siftp  localhost 1122 < ./commands2.txt &
cd ..

