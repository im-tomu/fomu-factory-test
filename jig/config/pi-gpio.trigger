[Trigger]
Name=Start button
Description=A start button on the Raspberry Pi jig, pin 26.  Compile with 'gcc gpiopoll.c -o gpiopoll; sudo chown root gpiopoll; sudo chmod u+s gpiopoll'
ExecStart=./gpiopoll 26
