## UID: 906279015

## Pipe Up

Low‑level C utility that chains up to eight executables exactly like the shell’s `|`, using `pipe(2)`, `fork(2)`, `dup2(2)`, and `execvp(3)`.

## Building

To build the program,  run this command 
```bash
make
```
## Running
```bash
cs111@cs111 ~/lab2 (main %) » ls
Makefile  pipe  pipe.c  pipe.o  __pycache__  README.md  test_lab1.py  test.py
cs111@cs111 ~/lab2 (main %) » ./pipe ls
Makefile  pipe  pipe.c  pipe.o  __pycache__  README.md  test_lab1.py  test.py
cs111@cs111 ~/lab2 (main %) » ls
Makefile  pipe  pipe.c  pipe.o  __pycache__  README.md  test_lab1.py  test.py
cs111@cs111 ~/lab2 (main %) » ./pipe ls cat wc
      8       8      71
cs111@cs111 ~/lab2 (main %) » ls | cat | wc
      8       8      71
```

## Cleaning up

To clean up all binary files, run this command
```bash
make clean
```
