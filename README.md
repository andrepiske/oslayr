
lua OS Layer

Execute processes in lua with bidirectional pipes for stdin and stdout (no stderr yet). Just because io.popen() is not bidirectional.

Linux only, but I *plan to port this to Windows someday* </sup>(TM)</sup>. 


Issues & feature request: "Issues" page on github.


Compile & Install
=============

Compile:

$ ./compile.sh

Install:

\# ./install.sh

The install script will install to /usr/lib/lua/5.1/oslayr.so
You may change that to work with lua5.2 (also change the compile.sh to link to lua5.2).

Usage
=====

This lib has 2 functions:

    exec(command, {arguments})

    exec_n_pipe(pipe_stderr, command, command_name, ...)

*exec*
--------
This will execute the program *command* with *arguments* and will pipe its stdout, stderr and stdin.

Note that arguments[1] will be the command`s argv[0] and so on.

**Return Value**

The return value is an userdata with the following functions:

    out([len=1024])
    put(content, [offset], [len])
    close()
    waitend()
    getpipesize()
    close_input()
    read(read_limit=1024, [source=1], [non-blocking=false])
    poll([timeout=0])

* **out()** will read the program stdout up to *len* bytes and will return two values. The first is how many bytes has been read and the second is the bytes read (a string). The first return value may be zero or lower. When zero, end-of-file has been reached. When negative, an error occured.

* **put()** will write *content* to stdin. The methods A and B in the example work exactly the same, although method A is faster.

        local proc = oslayr.exec('cat', {'cat'}) 
        local content = ...
        ... some very important code here ...
        proc:put(content, 10, 100) -- method A
        proc:put(string.gsub(content, 10, 110)) -- method B

* **close()** Closes the open program. It is very important that you call this after reading and writing everything. This method will free resources (buffers and file descriptors). It is safe to call waitend() after close() has been called. **NOTE** that this function will NOT kill the program, but will close its stdout, stdin and stderr, which may kill it via a unhandled SIGPIPE if the program tries to write to a broken pipe. **NOTE 2**  no that this won't be called automatically on garbage collection, so you **must** call this when appropriate.

* **waitend()** This will wait for the program end and will return its exit status. It will only return when the program ends. No implicit (nor explicit) timeouts here. This is the only function that is safe to be called after close().

* **getpipesize()** Returns three values (integers): The size (in bytes, in this order) of the stdin, stdout and stderr pipes. See caveats section below.

* **close_input()** Closes the stdin pipe. This will issue and end-of-file for the program reading the stdin.

* **read()** Reads from the process' stdout or stderr. This is more flexible version of *out()*. read\_limit is the *maximum* number of bytes to be read. source is the where to read from, either 1 or 2. 1 stands for stdout and 2 for stderr. No other values are allowed. non-blocking should be true or false. If true, it won't ever block. This means that if no input is available at all, it won't wait for some. It will return a 3-uple (success, count, data). _success_ is a boolean telling whether something has been read. It'll only be false on non-blocking calls when no input is available. The _count_ is a non-negative integer with the number of bytes read. This value may be zero, as for EOF. _data_ is a string with the read data, or nil in case _success_ equals false. **NOTE** Calling this function with non-blocking to false will return immediately if __any__ data is available to read; it won't wait to read all _read\_limit_ bytes. i.e., suppose one calls read(10, 1, false) and only 5 is available in stdout. The function will return immediately.

* **poll()** Checks whether input is available to be read. Returns a tuple (stdout, stderr) of booleans telling which is good to read. The timeout parameter must be a non-negative value. It specifies the timeout, in milliseconds, to wait. The timeout precision is microseconds, so one may use a value of "0.07" to wait 70 microsecnds.

**Examples**

Example 1. Parsing some text with some program:

    require('oslayr')
    local T = [[
    Save the foos!
    But shoot the bars!
    ]]
    local f = oslayr.exec('m4', {'m4', '-Dfoos=Whales', '-Dbars=Seals'})
    f:put(T)
    f:close_input()
    f:waitend()
    local l,c = f:out(65536)
    f:close()
    print(c)

Output:

    Save the Whales!
    But shoot the Seals!

Example 2. Shows the waitend() funcionality:

    require('oslayr')
    local f = oslayr.exec('false', {'false'})
    print(f:waitend())

Ouput:

    1

Example 3. Shows interactive usage:

    require('oslayr')
    local f = oslayr.exec('cat', {'cat', '-e'})
    f:put('Hel')
    f:put('lo')
    local l, x = f:out()
    print(x)
    f:put('\n')
    f:out() -- discard it
    f:put('foo\n')
    l, x = f:out()
    print(l)
    print(x)

Output:

    Hello
    5
    foo$

There are no buffers in put() function, so there is no need of "\n" to flush buffers (there are no write buffers!).

The length printed (the "5" you see there) are those 5 chars: { 'f', 'o', 'o', '$', '\n' }.

The '$' appears because of the '-e' argument (See cat(1)).

**Caveats**

Pipes are buffered by the kernel, but their buffer size is not unlimited. A value of 65536 bytes may be assumed for Linux and OS X, but the getpipesize() function returns the exact value.

When one tries to write to a pipe that is full, the put() function will block until the pipe gets free room, which happens when the other end of the pipe reads data from it.

The out() function blocks when there is no data available.

Here is an example of an application that will wait forever due to unlimited pipe buffers and blocks:

    require('oslayr')
    local f = oslayr.exec('cat', {'cat'})
    f:put(string.rep('.', 1024 * 500)) -- 500kB string

This program will block and won't ever continue for a pipe buffer size of 65kB and some theoretical implementation of the cat program. Here is what might happen (I'll refer "us" as this lua program and "them" as the cat program):

1. We write 500kB to the stdin pipe
2. They read 65kB from the stdin pipe
3. They write this same 65kB to their stdout pipe, which is what we get when we call f:out()
4. Now their stdout pipe is filled with 65kB
5. They read, let's say, 1 byte from their stdin.
6. They write this byte to their stdout. This write will block, as their stdout pipe is full.

To unblock their stdout pipe, we must f:out(), but we also are blocked. Dead End.

Question: how to overcome this? Answer: do not.
A buffered implementation of this very lib is planned to be written, some code is done, but time is needed and a lot of it is in this documentation.

*exec\_n\_pipe* 
---------------

This will execute the program *command*. *command\_name* is what the program will receive in argv[0].
Usually you'll want to use the same value for *command* and *command\_name*
The ellipsis are the other arguments to the program.

*pipe_stderr* is a boolean: When true it will include the stderr into the result value. When false it will ignore the stderr (it won't print the stderr anywhere. Instead, it will close the stderr file descriptor before running the program).

**Return Value**

This function returns **nil** on error (no permission to execute, no such program etc).

On success it will return a string of the output produced by the program. If *pipe\_stderr* is true then the return will also include the stderr output.

**Example Usage**

    require('oslayr')
    l = oslayr.exec_n_pipe(false, '/bin/ls' 'ls', '/')
    print(l)

Output:

    bin
    dev
    home
    sys
    opt
    root
    
It continues, but you got the idea.


