Fri Oct 23 08:29:06 PM IST 2026

test_net.c working
-> One process acts as a server and calls recv_all and 
    another forked process acts as a client and does send all
->  defined BIG_SIZE: 5MB the size of big transfer
            Slow_size: 64 the number of single bytes slow client sends
            SHORT_SENT/SHORT_WANT: the early-close test has the cliend send 100 bytes 
                                    while the server asks for 200.


fill_pattern(b,n): Fill a buffer with a predictable sequence the byte at position
                    i is i%251, so 0,1,2,...250,0 
                    The point is that receiver can recompute what every byte should be.
                    so if bytes arrive altered they wont match , 251 is prime 
                    so the pattern doesn't line up


check_pattern(b,n): We check the buffer and see if each byte equals i%251 return 0; 
                    if passed else -1

client_big(fd): Allocs 5MB fills it with pattern and sends with one single send_all
                the loop in send all has to run manytimes since kernel won't accept 
                5MB at once

client_slow(fd): Sends bytes one at a time with usleep(20000) 20 ms between them.
                the sleeps make server's recv return tiny peices, because only
                one byte has arrived when it asks, exposes any offset bugs in recv_all

client_short(fd): sends 100bytes and returns. back in run the child then closes the 
                    socket the server wanted 200, so it eventually see conn closed
                    early. That's the "peer hung up" case.


run(name, client, want, expect_ok)

This is the engine. Its arguments are the test's name, which client behaviour to use (a function pointer, 
which is what void (*client)(int) means: "a function taking an int and returning nothing"), 
how many bytes the server will ask for, and whether success or failure is the expected outcome.

Step by step:

Create a listening socket. 
    socket makes it, bind attaches it to localhost with addr.sin_port = 0, which means "OS, pick any free port", 
    so tests never collide with something already using a port. listen marks it as ready for connections.
getsockname asks the OS which port it chose and stores it back in addr, so the client knows where to connect.

fork() is the key trick. It copies the running process, and the call returns twice: 0 in the new child, 
and the child's process ID in the parent. 

The if (pid == 0) block is the child's code, and everything after it is the parent's.
Child (client): closes its copy of the listening socket, creates its own socket, connects to the parent's port, 
                runs the chosen client behaviour, closes the socket, and exits with _exit(0). 
                (_exit rather than exit, so the child doesn't flush output buffers it inherited from the parent.)

Parent (server): accept waits for the child's connection and returns a new socket for it, then the listening socket is closed since only one connection is needed.

The actual test: allocate want bytes and call recv_all. If expect_ok is true, the test passes only when recv_all returned 0 and the pattern checks out. 
                 If it's false (the early-close test), it passes only if recv_all returned -1.

Cleanup: close the connection, free the buffer, and waitpid so the finished child doesn't linger as a zombie process. Then print PASS or FAIL.
main

Calls run three times with different client behaviours and sizes, adds up failures, and returns nonzero if any test failed. That makes it usable in scripts later.
