#/bin/sh -v
#
# My test script to test my submission for sttyl.
# It makes the program, performs a few sample tests
# of my own, and runs the lib215-provided test script.
#

#-------------------------------------
#    compile program
#-------------------------------------
make clean

make sttyl


#-------------------------------------
#    my negative tests
#-------------------------------------

# illegal arguments
./sttyl foobar
./sttyl foo bar

# missing argument
./sttyl erase
./sttyl kill

# invalid integer argument (not a valid special char)
./sttyl erase foo
./sttyl erase +m

#-------------------------------------
#    run the course test-script
#-------------------------------------
~lib215/hw/stty/test.stty
