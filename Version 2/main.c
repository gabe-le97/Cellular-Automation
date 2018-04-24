//
//  main.c
//  Cellular Automaton
//
//  Created by Jean-Yves Hervé on 2018-04-09.
//  Edited by Gabe Le on 2018-04-14

/*------------------------------------------------------------------------------------------+
 |    A graphic front end for a grid+state simulation.                                      |
 |                                                                                          |
 |    This application simply creates a glut window with a pane to display                  |
 |    a colored grid and the other to display some state information.                       |
 |    Sets up callback functions to handle menu, mouse and keyboard events.                 |
 |    Normally, you shouldn't have to touch anything in this code, unless                   |
 |    you want to change some of the things displayed, add menus, etc.                      |
 |    Only mess with this after everything else works and making a backup                   |
 |    copy of your project.  OpenGL & glut are tricky and it's really easy                  |
 |    to break everything with a single line of code.                                       |
 |                                                                                          |
 |    Current keyboard controls:                                                            |
 |                                                                                          |
 |        - 'ESC' --> exit the application                                                  |
 |        - space bar --> resets the grid                                                   |
 |                                                                                          |
 |        - 'c' --> toggle color mode on/off                                                |
 |        - 'b' --> toggles color mode off/on                                               |
 |        - 'l' --> toggles on/off grid line rendering                                      |
 |                                                                                          |
 |        - '+' --> increase simulation speed                                               |
 |        - '-' --> reduce simulation speed                                                 |
 |                                                                                          |
 |        - '1' --> apply Rule 1 (Conway's classical Game of Life: B3/S23)                  |
 |        - '2' --> apply Rule 2 (Coral: B3/S45678)                                         |
 |        - '3' --> apply Rule 3 (Amoeba: B357/S1358)                                       |
 |        - '4' --> apply Rule 4 (Maze: B3/S12345)                                          |
 |                                                                                          |
 |  * String to compile the program by linking GLUT & pthread:                              |
 |      - gcc main.c gl_frontEnd.c -lm -lpthread -framework OpenGL -framework GLUT -o cell  |                                                           |
 +------------------------------------------------------------------------------------------*/

#include <stdio.h>          // for printf
#include <stdlib.h>         // for exit
#include <unistd.h>         // for stderror
#include <time.h>           // for usleep()
#include <pthread.h>        // for pthread_* calls
#include <semaphore.h>      // for semaphores
#include <sys/stat.h>       // for pipes
#include "gl_frontEnd.h"

// macros for MIN & MAX b/c C doesn't have them
#define MIN(X, Y) (((X) < (Y)) ? (X) : (Y))
#define MAX(X, Y) (((X) > (Y)) ? (X) : (Y))
//==================================================================================
//    Thread data type
//==================================================================================

typedef struct ThreadInfo {
    pthread_t threadID;
    int index;
} ThreadInfo;

//==================================================================================
//    Function prototypes
//==================================================================================

void displayGridPane(void);
void displayStatePane(void);
void initializeApplication(void);
void* threadFunc(void* arg);
void swapGrids(void);
void oneGeneration(int row, int col);
void lockCells(int row, int col);
void unlockCells(int row, int col);
void* threadFunction(void* arg);
void* namedPipeServer(void*);

unsigned int cellNewState(unsigned int i, unsigned int j);
// prevent double execution of threads in a row
sem_t mutex;

//==================================================================================
//    Precompiler #define to let us specify how things should be handled at the
//    border of the frame
//==================================================================================

#define FRAME_FIXED        -1    //    the one I demo-ed in class, now disabled (values at border are kept fixed)
#define FRAME_DEAD          0    //    cell borders are kept dead
#define FRAME_RANDOM        1    //    new random values are generated at each generation
#define FRAME_CLIPPED       2    //    same rule as elsewhere, with clipping to stay within bounds
#define FRAME_WRAP          3    //    same rule as elsewhere, with wrapping around at edges

// Pick one value for FRAME_BEHAVIOR
#define FRAME_BEHAVIOR    FRAME_DEAD

//==================================================================================
//    Application-level global variables
//==================================================================================

// Don't touch
extern const int GRID_PANE, STATE_PANE;
extern int gMainWindow, gSubwindow[2];

//    The state grid and its dimensions.  We now have two copies of the grid:
//        - currentGrid is the one displayed in the graphic front end
//        - nextGrid is the grid that stores the next generation of cell
//            states, as computed by our threads.
int* currentGrid;
int** currentGrid2D;
pthread_mutex_t * mutexLockGrid1D;
pthread_mutex_t **mutexLockGrid2D;

int numRows;
int numCols;
int maxNumThreads;
int numThreads;

int swapCounter;
int applicationSpeed = 100;

int fd1;
// able to hold the size of all command strings
char command[10];

// the number of live threads (that haven't terminated yet)
unsigned int numLiveThreads = 0;

// Rules of the automaton (in C, it's a lot more complicated than in
// C++/Java/Python/Swift to define an easy-to-initialize data type storing
// arrays of numbers.  So, in this program I hard-code my rules

unsigned int rule = GAME_OF_LIFE_RULE;

unsigned int colorMode = 0;

//==================================================================================
//    These are the functions that tie the simulation with the rendering.
//    Some parts are "don't touch."  Other parts need your intervention
//    to make sure that access to critical section is properly synchronized
//==================================================================================

void displayGridPane(void) {
    //    This is OpenGL/glut magic.  Don't touch
    glutSetWindow(gSubwindow[GRID_PANE]);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    
    //---------------------------------------------------------
    //    This is the call that makes OpenGL render the grid.
    //
    //---------------------------------------------------------
    drawGrid(currentGrid2D, numRows, numCols);
    
    //    This is OpenGL/glut magic.  Don't touch
    glutSwapBuffers();
    
    glutSetWindow(gMainWindow);
}

void displayStatePane(void) {
    //    This is OpenGL/glut magic.  Don't touch
    glutSetWindow(gSubwindow[STATE_PANE]);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    
    //---------------------------------------------------------
    //    This is the call that makes OpenGL render information
    //    about the state of the simulation.
    //
    //---------------------------------------------------------
    drawState(numLiveThreads);
    
    //    This is OpenGL/glut magic.  Don't touch
    glutSwapBuffers();
    
    glutSetWindow(gMainWindow);
}

/*
 *------------------------------------------------------------------------
 * Unique thread created the named pipe and handles communication between
 *------------------------------------------------------------------------
 */
void* namedPipeServer(void* arg) {
    FILE *fptr;
    char path[] = "/tmp/namedPipe";
    char readbuf[80];

    umask(0);
    mknod(path, S_IFIFO|0666, 0);

    // thread running... listening for commands
    while(1) {
        fptr = fopen(path, "r");
        fgets(readbuf, 80, fptr);
        pipeToCommand(readbuf);
        fclose(fptr);
    }
    return NULL;
}

/*
 *------------------------------------------------------------------------
 *   Main function where the threads are created
 *------------------------------------------------------------------------
 */
int main(int argc, char** argv) {
    // check if we have the correct parameters
    if(argc != 4) {
        printf("%s\n", "Wrong Number of Arguments");
        exit(-1);
    }
    
    // convert string arguments to integers to give the dimensions and no. threads
    sscanf(argv[1], "%d", &numRows);
    sscanf(argv[2], "%d", &numCols);
    sscanf(argv[3], "%d", &maxNumThreads);
    
    if(numRows < 5 || numCols < 5 | maxNumThreads > numCols) {
        printf("%s\n", "Incorrect Values as Dimensions or Threads");
        exit(-1);
    }

    // creating a thread for the named pipe to read constantly
    pthread_t namedpipeID;
    int pipeCode = pthread_create(&namedpipeID, NULL, namedPipeServer, NULL);
    // exit if we could not make a pipe
    if( pipeCode != 0) {
        printf ("could not create thread for pipe.\n");
        exit(0);
    }
    
    // This takes care of initializing glut and the GUI.
    // You shouldn’t have to touch this
    initializeFrontEnd(argc, argv, displayGridPane, displayStatePane);
    
    // Now we can do application-level initialization
    initializeApplication();
    
    // initialize the mutex lock & semaphore to prevent race condition
    
    // filling the mutex grid
    for(int i = 0; i < numRows; i++) {
        for(int j = 0; j < numCols; j++) {
            // control the access of all threads to a resource (swapping grids in this case)
            pthread_mutex_t myLock;
            pthread_mutex_init(&myLock, NULL);
            mutexLockGrid2D[i][j] = myLock;
        }
    }
    
    sem_init(&mutex, 0, 1);
    
    // figure out how many threads we need to create
    if(maxNumThreads > numRows) {
        numThreads = numRows;
    } else {
        numThreads = maxNumThreads;
    }
    
    // array for all the threads to easily access
    ThreadInfo threads[numThreads];
    int errCode;
    
    // create the threads we need that will run specific rows
    for (int i = 0; i < numThreads; i++) {
        threads[i].index = i+1;
        numLiveThreads++;
        errCode = pthread_create(&threads[i].threadID, NULL,
                                 threadFunc, threads + i);
        // stop if we could not create a thread
        if (errCode != 0) {
            printf("Could not create thread\n");
            exit (EXIT_FAILURE);
        }
    }
    
    //    Now we enter the main loop of the program and to a large extend
    //    "lose control" over its execution.  The callback functions that
    //    we set up earlier will be called when the corresponding event
    //    occurs
    glutMainLoop();
    //    In fact this code is never reached because we only leave the glut main
    //    loop through an exit call.
    //    Free allocated resource before leaving (not absolutely needed, but
    //    just nicer.  Also, if you crash there, you know something is wrong
    //    in your code.
    sem_destroy(&mutex);
    free(currentGrid2D);
    free(currentGrid);
    //    This will never be executed (the exit point will be in one of the
    //    call back functions).
    return 0;
}

void initializeApplication(void) {
    //  Allocate 1D grids
    //--------------------
    currentGrid = (int*) malloc(numRows*numCols*sizeof(int));

    //  Scaffold 2D arrays on top of the 1D arrays
    //---------------------------------------------
    currentGrid2D = (int**) malloc(numRows*sizeof(int*));
    currentGrid2D[0] = currentGrid;
    
    /// allocate mutexlock grid
    mutexLockGrid1D = (pthread_mutex_t*) malloc(numRows*numCols*sizeof(pthread_mutex_t));
    mutexLockGrid2D = (pthread_mutex_t**) malloc(numRows*sizeof(pthread_mutex_t*));
    mutexLockGrid2D[0] = mutexLockGrid1D;
    
    for (int i=1; i<numRows; i++) {
        currentGrid2D[i] = currentGrid2D[i-1] + numCols;
        mutexLockGrid2D[i] = mutexLockGrid2D[i-1] + numCols;
    }
    
    
    srand((unsigned int) time(NULL));
    resetGrid();
}

/*
 *------------------------------------------------------------------
 * Checks an entire row and changes which cells die and which
 *  survive
 *------------------------------------------------------------------
 */
void oneGeneration(int row, int col) {
    unsigned int newState = cellNewState(row, col);
    //    In black and white mode, only alive/dead matters
    //    Dead is dead in any mode
    if (colorMode == 0 || newState == 0) {
        currentGrid2D[row][col] = newState;
    }
    //    in color mode, color reflext the "age" of a live cell
    else {
        //    Any cell that has not yet reached the "very old cell"
        //    stage simply got one generation older
        if (currentGrid2D[row][col] < NB_COLORS-1)
            currentGrid2D[row][col] = currentGrid2D[row][col] + 1;
    }
}

/*
 *---------------------------------------------------------------------
 * Each thread will run indefinitely until we exit the application
 *.....................................................................
 * threads change generations for their assigned rows and mutex locks
 *  & semaphores are used for multithreading & keeping all threads
 *   in a phase
 *---------------------------------------------------------------------
 */
void* threadFunc(void* arg) {
    int row = 0;
    int col = 0;
    //  run the threads indefinitely until we stop the program
    while(1) {
        // change the cell of a random location
        row = rand()%numRows;
        col = rand()%numCols;
        lockCells(row, col);
        sem_wait(&mutex);
        oneGeneration(row, col);
        sem_post(&mutex);
        unlockCells(row, col);
        usleep(applicationSpeed);
    }
    return NULL;
}

/*
 *------------------------------------------------------------------
 *  locks the mutex and all its neighbors
 *------------------------------------------------------------------
 */
void lockCells(int row, int col) {
    pthread_mutex_trylock(&mutexLockGrid2D[row][col]);
    for(int x = MAX(0, row-1); x <= MIN(row+1, numRows); x++) {
        for(int y = MAX(0, col-1); y <= MIN(col+1, numCols); y++) {
            if(x != row || y != col) {
                // neighbors outside the grid are ignored
                if(x >= numRows || y >= numCols)
                    continue;
                else
                    pthread_mutex_trylock(&mutexLockGrid2D[x][y]);
            }
        }
    }
}

/*
 *------------------------------------------------------------------
 *  unlocks the mutex and all its neighbors
 *------------------------------------------------------------------
 */
void unlockCells(int row, int col) {
    pthread_mutex_unlock(&mutexLockGrid2D[row][col]);
    for(int x = MAX(0, row-1); x <= MIN(row+1, numRows); x++) {
        for(int y = MAX(0, col-1); y <= MIN(col+1, numCols); y++) {
            if(x != row || y != col){
                // neighbors outside the grid are ignored
                if(x >= numRows || y >= numCols)
                    continue;
                else
                    pthread_mutex_trylock(&mutexLockGrid2D[x][y]);
            }
        }
    }
}

/*
 *------------------------------------------------------------------
 *  Randomizes the grid at launch and everytime spacebar is pressed
 *------------------------------------------------------------------
 */
void resetGrid(void) {
    for (int i = 0; i < numRows; i++) {
        for (int j = 0; j < numCols; j++) {
            currentGrid2D[i][j] = rand() % 2;
        }
    }
}


/*
 *------------------------------------------------------------------
 *    The quick implementation I did in class is not good because it keeps
 *    the border unchanged.  Here I give three different implementations
 *    of a slightly different algorithm, allowing for changes at the border
 *    All three variants are used for simulations in research applications.
 *    I also refer explicitly to the S/B elements of the "rule" in place.
 *------------------------------------------------------------------
 */
unsigned int cellNewState(unsigned int i, unsigned int j) {
    // First count the number of neighbors that are alive
    //----------------------------------------------------
    //  Again, this implementation makes no pretense at being the most efficient.
    //  I am just trying to keep things modular and somewhat readable
    int count = 0;
    
    // Away from the border, we simply count how many among the cell's
    // eight neighbors are alive (cell state > 0)
    if (i > 0 && i < numRows-1 && j > 0 && j < numCols-1) {
        // remember that in C, (x == val) is either 1 or 0
        count = (currentGrid2D[i-1][j-1] != 0) +
        (currentGrid2D[i-1][j] != 0) +
        (currentGrid2D[i-1][j+1] != 0)  +
        (currentGrid2D[i][j-1] != 0)  +
        (currentGrid2D[i][j+1] != 0)  +
        (currentGrid2D[i+1][j-1] != 0)  +
        (currentGrid2D[i+1][j] != 0)  +
        (currentGrid2D[i+1][j+1] != 0);
    }
    // on the border of the frame...
    else {
#if FRAME_BEHAVIOR == FRAME_DEAD
        // Hack to force death of a cell
        count = -1;
        
#elif FRAME_BEHAVIOR == FRAME_RANDOM
        count = rand() % 9;
        
#elif FRAME_BEHAVIOR == FRAME_CLIPPED
        
        if (i > 0) {
            if (j>0 && currentGrid2D[i-1][j-1] != 0)
                count++;
            if (currentGrid2D[i-1][j] != 0)
                count++;
            if (j<numCols-1 && currentGrid2D[i-1][j+1] != 0)
                count++;
        }
        
        if (j>0 && currentGrid2D[i][j-1] != 0)
            count++;
        if (j<numCols-1 && currentGrid2D[i][j+1] != 0)
            count++;
        
        if (i<numRows-1) {
            if (j>0 && currentGrid2D[i+1][j-1] != 0)
                count++;
            if (currentGrid2D[i+1][j] != 0)
                count++;
            if (j<numCols-1 && currentGrid2D[i+1][j+1] != 0)
                count++;
        }
        
        
#elif FRAME_BEHAVIOR == FRAME_WRAPPED
        
        unsigned int     iM1 = (i+numRows-1)%numRows,
        iP1 = (i+1)%numRows,
        jM1 = (j+numCols-1)%numCols,
        jP1 = (j+1)%numCols;
        count = currentGrid2D[iM1][jM1] != 0 +
        currentGrid2D[iM1][j] != 0 +
        currentGrid2D[iM1][jP1] != 0  +
        currentGrid2D[i][jM1] != 0  +
        currentGrid2D[i][jP1] != 0  +
        currentGrid2D[iP1][jM1] != 0  +
        currentGrid2D[iP1][j] != 0  +
        currentGrid2D[iP1][jP1] != 0 ;
        
#else
#error undefined frame behavior
#endif
        
    }    // end of else case (on border)
    
    // Next apply the cellular automaton rule
    //----------------------------------------------------
    // by default, the grid square is going to be empty/dead
    unsigned int newState = 0;
    
    //    unless....
    
    switch (rule) {
            //    Rule 1 (Conway's classical Game of Life: B3/S23)
        case GAME_OF_LIFE_RULE:
            
            //    if the cell is currently occupied by a live cell, look at "Stay alive rule"
            if (currentGrid2D[i][j] != 0) {
                if (count == 3 || count == 2)
                    newState = 1;
            }
            //    if the grid square is currently empty, look at the "Birth of a new cell" rule
            else {
                if (count == 3)
                    newState = 1;
            }
            break;
            
            //    Rule 2 (Coral Growth: B3/S45678)
        case CORAL_GROWTH_RULE:
            
            //    if the cell is currently occupied by a live cell, look at "Stay alive rule"
            if (currentGrid2D[i][j] != 0) {
                if (count > 3)
                    newState = 1;
            }
            //    if the grid square is currently empty, look at the "Birth of a new cell" rule
            else {
                if (count == 3)
                    newState = 1;
            }
            break;
            
            //    Rule 3 (Amoeba: B357/S1358)
        case AMOEBA_RULE:
            
            //    if the cell is currently occupied by a live cell, look at "Stay alive rule"
            if (currentGrid2D[i][j] != 0) {
                if (count == 1 || count == 3 || count == 5 || count == 8)
                    newState = 1;
            }
            //    if the grid square is currently empty, look at the "Birth of a new cell" rule
            else {
                if (count == 1 || count == 3 || count == 5 || count == 8)
                    newState = 1;
            }
            break;
            
            // Rule 4 (Maze: B3/S12345)                            |
        case MAZE_RULE:
            
            // if the cell is currently occupied by a live cell, look at "Stay alive rule"
            if (currentGrid2D[i][j] != 0) {
                if (count >= 1 && count <= 5)
                    newState = 1;
            }
            // if the grid square is currently empty, look at the "Birth of a new cell" rule
            else {
                if (count == 3)
                    newState = 1;
            }
            break;
            
            break;
            
        default:
            printf("Invalid rule number\n");
            exit(5);
    }
    
    return newState;
}
