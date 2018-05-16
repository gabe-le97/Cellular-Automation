# Cellular-Automation
Runs 4 versions of an application where cells die & grow depending on their location
***
Rule 1: Conway's classic Game of Life
![rule1](https://raw.githubusercontent.com/gabe-le97/Cellular-Automation/master/img/rule1.png)

Rule 2: Coral
![rule2](https://raw.githubusercontent.com/gabe-le97/Cellular-Automation/master/img/rule2.png)

Rule 3: Amoeba
![rule3](https://raw.githubusercontent.com/gabe-le97/Cellular-Automation/master/img/rule3.png)

Rule 4: Maze
![rule4](https://raw.githubusercontent.com/gabe-le97/Cellular-Automation/master/img/rule4.png)
***
This program takes as parameters: __./filename numberOfRows numberOfColumns numberOfThreads__
***
__Bash Script__: compiles and launches either version & controls the application
through strings instead of keypresses
* rule # (# is 1 - 4), color on, color off, speedup, slowdown, end
***
__Controls__:
* ESC -> closes the application
* Spacebar -> resets the grid
* c -> toggle color mode on/off
* b -> toggle color mode on/off
* l -> toggle grid mode on/off
* ++ -> speed up simulation speed
* -- -> slow down simulation speed
***
__Version 1__: Multithreaded with a single mutex lock
* Each thread will be assigned a select number of rows to change generations
* Mutex locks will be used to prevent race condition between the 2D grids
* Semaphore is used to keep threads in phase

__Version 2__: 
* Each thread will select a single random cell to change generations
* There is a grid of mutex locks and the thread must have access to the cell
and all 8 neighbors to change the cell
* Semaphore is used to keep threads in phase
