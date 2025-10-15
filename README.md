<--------------------------------------------------->
MY TFTP PROJECT v0.01
<--------------------------------------------------->

This project is made by Daniel Pier, any proposal to make
the project function better or even ideas would be gladly appericiated and
thought of.

This project is a TFTP Netascii/Octet based code written in C by me with some twists to it such as:

1. Giving the user the functionality to create a netascii file as the code runs and send it as he tries to send an WRQ request.
2. Giving the user the functionality to print (later on the user could also be able to run the program, for now I haven't created a permission giver per file that has been downloaded via RRQ)
3. ACK - REACK functionality
4. Multi-Threaded in a sense, since it works with 10 ports and it accepts any request then it should be multi-threaded.

It suppose to be available in the final version for windows and MACs aswell,
because of some functions that are suppose to make it run and some CRLF issues 
that I haven't implemented for now.

it is available for Linux for now,
to run it you need to get to the terminal,
cd to the path of the project,
type make and hit enter,
and then run it with ./tftp_server_r, ./tftp_client_r


That's one of my first big projects so far, and hopefully will get better later on :) .

Feel free to try it out! 
