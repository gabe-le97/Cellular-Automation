#-----------------------------------------------------
# Created by Gabe Le
# 4/20/18
#......................................................
# A script for launching the cellular automaton process, 
#  and communicate with the program through a named namedPipe
#......................................................
# My test arguments
# ./script.sh 200 203 4
#-----------------------------------------------------

# checking the number of arguments 
if [ $# -ne 3 ]; then
    echo "invalid number of arguments"
    exit 1
fi

# checking if the arguments are positive integers
re='^-?[0-9]+([.][0-9]+)?$'
for var in "$@"
do
	if ! [[ $var =~ $re ]] || [[ $var < 0 ]]; then
	   echo "error: Not a number" >&2
	   exit 1
	fi
done

# check if number of threads is valid
if [[ $3 -gt $1 ]] ; then
	echo "wrong number of threads"
	exit 1
fi

# check which version we want to run 
keepGoing=1
while [[ $keepGoing -eq 1 ]]; do
	read -p "Run which Version: 1 or 2: " version
	if [[ $version -eq 1 ]] || [[ $version -eq 2 ]] ; then
		keepGoing=0
	else
		echo "please enter 1 or 2"	
	fi
done

# go into version directory 
if [[ $version -eq 1 ]]; then
	cd "Version 1"
elif [[ $version -eq 2 ]]; then
	cd "Version 2"
fi

# compile the main c file
# gcc main.c gl_frontEnd.c -lm -lpthread -framework OpenGL -framework GLUT -o cell
gcc main.c gl_frontEnd.c -lGL -lglut -lpthread -o cell

# launch the program in the background
./cell $1 $2 $3 &

#-----------------------------------------------------
# go into directory with the named pipe
cd /tmp/

# make sure we can write to the pipe
if [[ ! -p namedPipe ]]; then
    echo "Reader not running"
    exit 1
fi

# pass the rules to the pipe & cell program
keepGoing=1
while [[ $keepGoing -eq 1 ]]; do
	read -p "Enter a command: " commands
	# changing the reproduction rule
	if [[ $commands = "rule 1" ]]; then
		echo $commands>namedPipe
		echo "Classic Mode"
	elif [[ $commands = "rule 2" ]]; then 
		echo $commands>namedPipe
		echo "Coral Mode"
	elif [[ $commands = "rule 3" ]]; then 
		echo $commands>namedPipe
		echo "Amoeba Mode"
	elif [[ $commands = "rule 4" ]]; then 
		echo $commands>namedPipe
		echo "Maze Mode"
#-----------------------------------------		
	elif [[ $commands = "color on" ]]; then 
		echo $commands>namedPipe
		echo "Color Mode on"
	elif [[ $commands = "color Mode Off" ]]; then
		echo $commands>namedPipe
		echo "Color Mode Off"
	elif [[ $commands = "speedup" ]]; then
		echo $commands>namedPipe
		echo "Speed Up"
	elif [[ $commands = "slowdown" ]]; then
		echo $commands>namedPipe
		echo "Slow Down"
	elif [[ $commands = "end" ]]; then
		echo "Goodbye"
		echo $commands>namedPipe 
		exit 0
	else
		echo "Please enter a valid command" 
	fi
done







