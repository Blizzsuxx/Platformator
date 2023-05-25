#!/bin/bash

# Obtain the list of files in the current directory with .h or .cpp extension
OBJS=$(ls *.h *.cpp 2>/dev/null | tr '\n' ' ')

# Set the compiler, compiler flags, linker flags, and object name
CC="g++"
COMPILER_FLAGS="-w"
LINKER_FLAGS="-lSDL2 -lSDL2_image -lSDL2_ttf -lSDL2_mixer"
OBJ_NAME="main"

# Create the Makefile
cat <<EOF > Makefile
OBJS = ${OBJS}
CC = ${CC}
COMPILER_FLAGS = ${COMPILER_FLAGS}
LINKER_FLAGS = ${LINKER_FLAGS}
OBJ_NAME = ${OBJ_NAME}

all : \$(OBJS)
	\$(CC) \$(OBJS) \$(COMPILER_FLAGS) \$(LINKER_FLAGS) -o \$(OBJ_NAME)
EOF

echo "Makefile created successfully."
echo "Compiling..."
make
echo "Compilation complete."
echo "Running..."
./${OBJ_NAME}
echo "Program terminated."