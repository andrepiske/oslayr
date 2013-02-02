
lua OS Layer

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

This lib has 1 function:

    exec_n_pipe(pipe_stderr, command, command_name, ...)

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
    
I continues, but you got the idea.


