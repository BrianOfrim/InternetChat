CMPUT 379 Assignment 2
Brian Ofrim (1374053) & Dai Dao (????????)

Compile with:
	make
Clean up with 
	make clean

Example run:
	./server379 2222
	./chat379 localhost 2222 name

Important notes: ********************************************************************

When you run 'make' expect a lot of compiler warnings but no errors.

SERVER DOES NOT WORK CONSISTANTLY AS A DAEMON. But will work fine as a normal process.
The sockets will remain active but pipes that the server uses for internal communication will
quit unexpectedlty. You can try to run it as a daemon a few times but if it does not provide 
chat functionality then comment out daemonize() on line 605 (first line in server.c main function) 
then recompile with 'make' and it will run as a normal process. The log file will not be filled 
out if it is run as a normal process.

In the chat client entering the single character '*' will display all of the other clients 
currently involed it the chat. Any other input will result in a message being sent to the other clients.
 
Logfile location: /var/tmp

Uses INET socket but only tested on localhost so please only use localhost!!!

When you run the server as a daemon make sure when you want to end it that you run both of the following:
pkill server379
pkill -9 server379

Sources:
	The lab slides and examples
	http://www.linuxhowtos.org/C_C++/socket.htm

Thank you and if you have any questions with the above then please contact us.



