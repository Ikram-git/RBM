#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <time.h>
#include <math.h>

// directory of the analyzer file
#define DIRECTORY_ANALYSIS_REPORT "RBM_Report_G23_analysis.txt"

// the character limit of a word
#define WORD_CHAR_LIMIT 25
// the word limit of a command
#define COMMAND_CHAR_SIZE 80
// the maximum command can stored in the system
#define MAX_COMMAND 300
// directory of the output file
#define DIRECTORY_OUTPUT_PREFIX "RBM_Report_G23_out"

//  -- Common Data --
// Tenants, rooms, and facilities
// EACH NAME CANNOT EXCCED WORD_CHAR_LIMIT
const char *tenantNames[] = {"tenant_A", "tenant_B", "tenant_C", "tenant_D", "tenant_E"};
const char *roomNames[] = {"room_A", "room_B", "room_C"};
const char *webcamNames[] = {"webcam_FHD", "webcam_FHD", "webcam_UHD"};
const char *monitorNames[] = {"monitor_50", "monitor_50", "monitor_75"};
const char *projectorNames[] = {"projector_2K", "projector_2K", "projector_4K"};
const char *screenNames[] = {"screen_100", "screen_100", "screen_150"};

const int roomCapacity[] = {10, 10, 20};

#define tenantAmount (sizeof(tenantNames) / sizeof(const char *))
#define roomAmount (sizeof(roomNames) / sizeof(const char *))
#define webcamAmount (sizeof(webcamNames) / sizeof(const char *))
#define monitorAmount (sizeof(monitorNames) / sizeof(const char *))
#define projectorAmount (sizeof(projectorNames) / sizeof(const char *))
#define screenAmount (sizeof(screenNames) / sizeof(const char *))

#define SCHEDULE_STRING_LENGTH 10 * 7 + (roomAmount + webcamAmount + monitorAmount + projectorAmount + screenAmount) * 24 * 7

// scedule data (10-05-2021 to 16-05-2021)

// FORMAT:
//[DATE(length:10)][room_A schedule(length:24)][room_B schedule]...[webcam[0] schedule(length: 24)]...[monitor, project, screen]
//(repeat after all room/device schedule is inserted on a single date)[next DATE]...

// EXPLAIN:
// in room/device each schedule: 24 hour is 24 char, if no one used on that time, '_' is insert
// if someone is used, insert the tenant index in tenantNames

// sample schedule:
// 10-05-2021__000___111___123...(until finish the room_A schedule)[all room schedule][all device schedule...]
// note: device type schedule order: webcam, monitor, projector, screen
// note: on each device type: the order of device schedule is listed in [XXX]Names (start from line 21)

char fcfs[SCHEDULE_STRING_LENGTH];
char priority[SCHEDULE_STRING_LENGTH];
int fcfsRejectedCount = 0;
int fcfsRejectedCommandIndex[MAX_COMMAND];
int priorityRejectedCount = 0;
int priorityRejectedCommandIndex[MAX_COMMAND];

//  -- Utility functions --
// find number of word in a string (seperate by space)
// ASSUMING NO SPACE FORMAT ERROR
int getNumOfWord(char *command, int numOfChar)
{
    int i, wordCount = 0;
    int isPreviousSpace = 1;
    for (i = 0; i < numOfChar; i++)
    {
        if (command[i] == ' ')
        {
            isPreviousSpace = 1;
        }
        else if (isPreviousSpace == 1)
        {
            isPreviousSpace = 0;
            wordCount++;
        }
    }
    return wordCount;
}

// split whole command to a string array (seperate by space), delete ';'
// ASSUMING NO SPACE FORMAT ERROR
// !!! REMEMBER to free the memory after using the return value
char **splitCommand(char *command, int numOfChar)
{
    int i, wordCount = getNumOfWord(command, numOfChar);
    // printf("Function 'splitCommand': wordCount = %d\n", wordCount);
    char **words = malloc(wordCount * sizeof(char *));
    for (i = 0; i < wordCount; i++)
    {
        words[i] = malloc(WORD_CHAR_LIMIT * sizeof(char));
    }
    char tempWord[25];
    int tempWordCount = 0;
    int tempCharCount = 0;
    for (i = 0; i < numOfChar; i++)
    {
        if (command[i] == ' ')
        {
            words[tempWordCount][tempCharCount] = 0;
            tempWordCount++;
            tempCharCount = 0;
        }
        else if (command[i] == ';')
        {
            words[tempWordCount][tempCharCount] = 0;
            break;
        }
        else
        {
            words[tempWordCount][tempCharCount] = command[i];
            tempCharCount++;
        }
    }
    for (i = 0; i < wordCount; i++)
    {
        // printf("Function 'splitCommand': word[%d] = %s\n", i, words[i]);
    }
    return words;
}

// convert int to string
// !!! REMEMBER to free the memory after using the return value
char *getStringFromInt(int number)
{
    int length = snprintf(NULL, 0, "%d", number);
    char *result = malloc(length + 1);
    snprintf(result, length + 1, "%d", number);
    return result;
}

// convert string to int
int getIntFromString(char *str)
{
    return strtol(str, NULL, 10);
}

int charToInt(char c)
{
    int num = 0;

    //Substract '0' from entered char to get
    //corresponding digit
    num = c - '0';

    return num;
}

int main()
{
    printf("~~ WELCOME TO PolySME ~~ \n");
    // STEP 1: fork and pipe for INPUT MODULE
    int inputPipe[2][2];
    pid_t inputCid;
    // create pipe, parent use [0][1] write, child use [1][1] write
    int i;
    for (i = 0; i < 2; i++)
    {
        if (pipe(inputPipe[i]) < 0)
        {
            printf("Error: Pipe creation error\n");
            exit(1);
        }
    }

    // create INPUT MODULE child process
    if ((inputCid = fork()) < 0)
    {
        printf("Error: fork input module error\n");
        exit(1);
    }
    else if (inputCid == 0)
    {
        // INPUT MODULE
        close(inputPipe[1][0]);
        close(inputPipe[0][1]);
        // printf("Input Moudle: Started.\n");

        char buf[COMMAND_CHAR_SIZE];
        char parentBuf[COMMAND_CHAR_SIZE];
        FILE *infilep = NULL;
        int invalid = 0;
        int n;
        int startBatch = 0;
        char **words;
        int wordCount;
        while (1)
        {
            if (invalid == 1)
            {
                write(inputPipe[1][1], "INVALID", COMMAND_CHAR_SIZE);
                invalid = 0;
            }
            // Read Input
            int i;
            // printf("Input Moudle: infilep = %d.\n", infilep == NULL);
            if (infilep == NULL)
            {
                n = read(inputPipe[0][0], parentBuf, COMMAND_CHAR_SIZE);
                parentBuf[n] = 0;
                if (strcmp(parentBuf, "PROMPT") == 0)
                {
                    // read from stdin
                    printf("Please enter booking:\n");
                    n = read(STDIN_FILENO, buf, COMMAND_CHAR_SIZE);
                    if (n <= 1)
                    {
                        printf("Input Module Error: entering empty line\n");
                        invalid = 1;
                        continue;
                    }
                    buf[--n] = 0;
                    // printf("Input Module: %d char in input line: [%s]\n", n, buf);
                }
                else
                {
                    printf("Input Error: unexpected pipe [%s]\n", buf);
                }
            }
            else
            {
                // read from file
                if (fgets(buf, COMMAND_CHAR_SIZE - 1, infilep))
                {
                    if (startBatch == 1)
                    {
                        buf[strcspn(buf, "\n")] = 0;
                        n = strlen(buf);
                        startBatch = 0;
                    }
                    else
                    {
                        n = read(inputPipe[0][0], parentBuf, COMMAND_CHAR_SIZE);
                        parentBuf[n] = 0;
                        if (strcmp(parentBuf, "PROMPT") == 0)
                        {
                            buf[strcspn(buf, "\n")] = 0;
                            n = strlen(buf);
                            // printf("Input Modul: batch read line %s: %d\n", buf, n);
                        }
                    }
                }
                else
                {
                    fclose(infilep);
                    infilep = NULL;
                    continue;
                }
            }

            // Error handling
            // can handle: wrong word count, wrong tenant name, wrong device name
            words = splitCommand(buf, n);
            wordCount = getNumOfWord(buf, n);
            int deviceIndex = -1;
            if (strcmp(words[0], "addMeeting") == 0)
            {
                words[1]++;
                deviceIndex = 6;
            }
            else if (strcmp(words[0], "addPresentation") == 0)
            {
                words[1]++;
                deviceIndex = 6;
            }
            else if (strcmp(words[0], "addConference") == 0)
            {
                words[1]++;
                deviceIndex = 6;
            }
            else if (strcmp(words[0], "bookDevice") == 0)
            {
                words[1]++;
                deviceIndex = 5;
            }
            // word check
            if (deviceIndex != -1)
            {
                if (wordCount < deviceIndex)
                {
                    printf("Input Module Error: must have at least %d words\n", deviceIndex);
                    invalid = 1;
                    continue;
                }
                // tenant name check
                int valid = 0;
                for (i = 0; i < tenantAmount; i++)
                {
                    if (strcmp(words[1], tenantNames[i]) == 0)
                    {
                        valid = 1;
                        break;
                    }
                }
                if (valid == 0)
                {
                    printf("Input Module Error: invalid tenant\n");
                    invalid = 1;
                    continue;
                }
                // device name check
                int deviceValid = 1;
                int j, k;
                for (i = deviceIndex; i < wordCount; i++)
                {
                    valid = 0;
                    for (j = 0; j < webcamAmount; j++)
                    {
                        if (strcmp(words[i], webcamNames[j]) == 0)
                        {
                            valid = 1;
                            break;
                        }
                    }
                    for (j = 0; j < monitorAmount; j++)
                    {
                        if (strcmp(words[i], monitorNames[j]) == 0)
                        {
                            valid = 1;
                            break;
                        }
                    }
                    for (j = 0; j < projectorAmount; j++)
                    {
                        if (strcmp(words[i], projectorNames[j]) == 0)
                        {
                            valid = 1;
                            break;
                        }
                    }
                    for (j = 0; j < screenAmount; j++)
                    {
                        if (strcmp(words[i], screenNames[j]) == 0)
                        {
                            valid = 1;
                            break;
                        }
                    }
                    if (valid == 0)
                    {
                        deviceValid = 0;
                    }
                }
                if (deviceValid == 0)
                {
                    printf("Input Module Error: invalid device\n");
                    invalid = 1;
                    continue;
                }
            }

            // handle addBatch command
            if (strcmp(words[0], "addBatch") == 0)
            {
                words[1]++;
                infilep = fopen(words[1], "r");
                if (infilep == NULL)
                {
                    printf("Input Module: error openning file\n");
                    invalid = 1;
                }
                else
                {
                    startBatch = 1;
                }
                continue;
            }

            free(words);

            // Transfer to parent using pipe
            if (invalid == 0)
            {
                write(inputPipe[1][1], buf, COMMAND_CHAR_SIZE);
            }

            // Determine whether to end program
            //printf("Input Module: Input line [%s] written to pipe\n", buf);
            if (strcmp(buf, "endProgram;") == 0)
            {
                // printf("Input Module: end program command is received.\n");
                break;
            }
        }

        // clean up
        close(inputPipe[1][1]);
        close(inputPipe[0][0]);
        // printf("Input Moudle: I have completed!.\n");
    }
    else
    {
        // parent process
        close(inputPipe[1][1]);
        close(inputPipe[0][0]);

        // STEP 2: fork/pipe SCHEDULING MODULE
        int schedulePipe[2][2];
        pid_t scheduleCid;
        // create pipe, parent use [0][1] write, child use [1][1] write
        for (i = 0; i < 2; i++)
        {
            if (pipe(schedulePipe[i]) < 0)
            {
                printf("Error: input pipe creation error\n");
                exit(1);
            }
        }
        if ((scheduleCid = fork()) < 0)
        {
            printf("Error: fork scheduling module error\n");
            exit(1);
        }
        else if (scheduleCid == 0)
        {
            // TODO: Scheduling module (Part 2)
            // -- USEFUL DATA --
            // storing all the command
            // note: command can be split using splitCommand()
            char commandHistory[MAX_COMMAND][COMMAND_CHAR_SIZE];
            // storing the number of command
            int numOfCommand = 0;
            // storing the mode of scheduling
            char mode[10];

            // closed unused pipes
            close(inputPipe[0][1]);
            close(inputPipe[1][1]);
            close(schedulePipe[1][0]);
            close(schedulePipe[0][1]);
            // printf("Schedule Module: Created.\n");
            // waiting to receive command history from parent
            int n;
            int isFirstLine = 1;
            char buf[COMMAND_CHAR_SIZE];
            while (1)
            {
                n = read(schedulePipe[0][0], buf, COMMAND_CHAR_SIZE);
                buf[n] = 0;
                //printf("Scheduling module: received [%s]\n", buf);
                // whole program is finish, end child
                if (strcmp(buf, "CLOSE") == 0)
                {
                    // printf("Scheduling module: closing...\n");
                    break;
                }
                if (isFirstLine == 1)
                {
                    strcpy(mode, buf);
                    //printf("mode: %s\n", mode);
                    isFirstLine = 0;
                }
                else
                {
                    strcpy(commandHistory[numOfCommand], buf);
                    numOfCommand++;
                }
                if (strcmp(buf, "END") == 0)
                {
                    char fcfsSchedule[SCHEDULE_STRING_LENGTH + 1];
                    char prioritySchedule[SCHEDULE_STRING_LENGTH + 1];
                    int fcfsRejectedCount = 0;
                    int fcfsRejectedCommandIndex[MAX_COMMAND];
                    int priorityRejectedCount = 0;
                    int priorityRejectedCommandIndex[MAX_COMMAND];
                    //mark the priority level, higher value higher level
                    int prioritySchedule_p[SCHEDULE_STRING_LENGTH + 1] = {[0 ... SCHEDULE_STRING_LENGTH] = 0};
                    //mark the index of command, default = -1
                    int prioritySchedule_c[SCHEDULE_STRING_LENGTH + 1] = {[0 ... SCHEDULE_STRING_LENGTH] = -1};

                    // All data is received from parent
                    // -- Test Data(Can be deleted) --
                    /*
                    for (i = 0; i < 7; i++)
                    {
                        char *tempDay = getStringFromInt(i);
                        strcat(fcfsSchedule, "2021-05-1");
                        strcat(prioritySchedule, "2021-05-1");
                        strcat(fcfsSchedule, tempDay);
                        strcat(prioritySchedule, tempDay);
                        int limit = (roomAmount + webcamAmount + monitorAmount + projectorAmount + screenAmount) * 24;
                        int j;
                        for (j = 0; j < limit; j++)
                        {
                            if (i == 4)
                            {
                                strcat(fcfsSchedule, "0");
                                strcat(prioritySchedule, "1");
                            }
                            else if (i == 6)
                            {
                                strcat(fcfsSchedule, "0");
                                strcat(prioritySchedule, "0");
                            }
                            else
                            {
                                strcat(fcfsSchedule, "_");
                                strcat(prioritySchedule, "_");
                            }
                        }
                        free(tempDay);
                    }
                    fcfsRejectedCount = 1;
                    fcfsRejectedCommandIndex[0] = 3;
                    priorityRejectedCount = 1;
                    priorityRejectedCommandIndex[0] = 2;
                    */

                    //  -- Write it here

                    // main objectives:
                    // 1. make the schedule by reading the commmad(need commadHistory, numOfCommad)
                    // 2. put the schedule in the fcfsSchedule / prioritySchedule
                    // if mode = -fcfs, put the fcfs schedule to fcfsSchedule var
                    // if mode = -priority, put the priority schedule to prioritySchedule var
                    // if mode = -ALL, put fcfs schedule to fcfsSchedule var, and priority to prioritySchedule var
                    // 3. mark the amount of rejected command to [fcfs/priority]RejectedCount
                    // 4. mark all rejected command index in [fcfs/priority]RejectedCommandIndex

                    // use schedulePipe[1][1] to WRITE TO PARENT
                    // use schedulePipe[0][0] to READ FROM PARENT

                    typedef struct sub_item
                    {
                        int offset;
                        int length;
                    } sub_item;

                    typedef enum
                    {
                        none,
                        bookDevice,
                        addMeeting,
                        addPresentation,
                        addConference,
                    } event_priority;

                    int timeSlotSize = 24;
                    int date_Length = 10;
                    sub_item I_room = {date_Length, timeSlotSize};                                                   //3, A,B,C = roomCapacity
                    sub_item I_webcam = {I_room.offset + (I_room.length * roomAmount), timeSlotSize};                //3 FHD,FHD,UHD
                    sub_item I_monitor = {I_webcam.offset + (I_webcam.length * webcamAmount), timeSlotSize};         //3 50,50,75
                    sub_item I_projector = {I_monitor.offset + (I_monitor.length * monitorAmount), timeSlotSize};    //3 2K,2K,4K
                    sub_item I_screen = {I_projector.offset + (I_projector.length * projectorAmount), timeSlotSize}; //3 100,100,150
                    int day_Length = I_screen.offset + (I_screen.length * screenAmount);

                    //initialize
                    if ((sizeof(fcfsSchedule) / sizeof(const char *)) > 1)
                    {
                        memset(fcfsSchedule, 0, (SCHEDULE_STRING_LENGTH + 1) * (sizeof(fcfsSchedule[0])));
                        memset(prioritySchedule, 0, (SCHEDULE_STRING_LENGTH + 1) * (sizeof(prioritySchedule[0])));
                        memset(fcfsRejectedCommandIndex, 0, (MAX_COMMAND + 1) * (sizeof(fcfsRejectedCommandIndex[0])));
                        memset(priorityRejectedCommandIndex, 0, (MAX_COMMAND + 1) * (sizeof(priorityRejectedCommandIndex[0])));
                    }
                    for (i = 0; i < 7; i++)
                    {
                        char *tempDay = getStringFromInt(i);
                        strcat(fcfsSchedule, "2021-05-1");
                        strcat(prioritySchedule, "2021-05-1");
                        strcat(fcfsSchedule, tempDay);
                        strcat(prioritySchedule, tempDay);
                        int limit = (roomAmount + webcamAmount + monitorAmount + projectorAmount + screenAmount) * 24;
                        int j;
                        for (j = 0; j < limit; j++)
                        {
                            strcat(fcfsSchedule, "_");
                            strcat(prioritySchedule, "_");
                        }
                        free(tempDay);
                    }

                    int S_numOfWords;
                    char **S_words;
                    int S_CommandIndex; //for looping use
                    event_priority eventType;
                    //bbbbbbbb
                    for (S_CommandIndex = 0; S_CommandIndex < numOfCommand; S_CommandIndex++)
                    { //for each command
                        // identify command
                        eventType = none;

                        //seperate command to word
                        S_numOfWords = getNumOfWord(commandHistory[S_CommandIndex], strlen(commandHistory[S_CommandIndex]));
                        S_words = splitCommand(commandHistory[S_CommandIndex], strlen(commandHistory[S_CommandIndex]));

                        printf("%d: %s\n", S_CommandIndex + 1, S_words[0]);
                        if (strcmp(S_words[0], "END") == 0)
                        {
                            break;
                        }

                        if (strcmp(S_words[0], "addMeeting") == 0)
                        { //prio = 2
                            eventType = addMeeting;
                        }
                        else if (strcmp(S_words[0], "addPresentation") == 0)
                        { //prio = 3
                            eventType = addPresentation;
                        }
                        else if (strcmp(S_words[0], "addConference") == 0)
                        { //prio = 4
                            eventType = addConference;
                        }
                        else if (strcmp(S_words[0], "bookDevice") == 0)
                        { //prio = 1
                            eventType = bookDevice;
                        }
                        else
                        {
                            //printf("invalid input at Scheduling module\n");
                            continue;
                        }

                        char tempStr[WORD_CHAR_LIMIT];
                        int cur_day_offset = charToInt(S_words[2][9]) * day_Length; //YYYY-MM-D[D]
                        strncpy(tempStr, S_words[3], 2);                            //[hh]:mm
                        int cur_time = getIntFromString(tempStr);                   //[hh]:mm
                        int cur_dur = charToInt(S_words[4][0]);                     //[n].n

                        int fcfs_rej_flag = 0;
                        int prio_rej_flag = 0;

                        // check room part
                        int fcfsSelectedRoom = -1;
                        int prioSelectedRoom = -1;

                        if ((eventType != none) && (eventType != bookDevice))
                        { //plan room, if addMeeting addPresentation addConference

                            int cur_person = getIntFromString(S_words[5]); //[p]
                            int cur_roomSuitable[roomAmount] = {[0 ... roomAmount - 1] = 1};
                            int cur_roomSuitableCount = 0;

                            for (i = 0; i < roomAmount; i++)
                            {
                                if (cur_person > roomCapacity[i])
                                {
                                    cur_roomSuitable[i] = 0;
                                }
                                else
                                {
                                    cur_roomSuitableCount++;
                                }
                            }
                            if (cur_roomSuitableCount == 0)
                            {
                                fcfs_rej_flag = 1;
                                prio_rej_flag = 1;
                            }

                            int fcfsSectionNotAvailableFlag[roomAmount] = {[0 ... roomAmount - 1] = 0};
                            int prioSectionNotAvailableFlag[roomAmount] = {[0 ... roomAmount - 1] = 0};
                            int prioSectionNotdisplaceableFlag[roomAmount] = {[0 ... roomAmount - 1] = 0};
                            int prioSectiondisplaceablePrio[roomAmount] = {[0 ... roomAmount - 1] = 0};
                            for (i = 0; i < roomAmount; i++)
                            { //loop all room
                                int j;
                                if (cur_roomSuitable[i] == 1)
                                { // only check the room with suitable size
                                    fcfsSectionNotAvailableFlag[i] = 0;
                                    prioSectionNotAvailableFlag[i] = 0;
                                    prioSectionNotdisplaceableFlag[i] = 0;
                                    for (j = 0; j < cur_dur; j++)
                                    {
                                        //check room available
                                        int checkSlot = cur_day_offset + I_room.offset + (I_room.length * i) + cur_time + j;
                                        if (fcfsSchedule[checkSlot] != '_')
                                        {
                                            fcfsSectionNotAvailableFlag[i] = 1;
                                        }
                                        if (prioritySchedule_p[checkSlot] > 0)
                                        {
                                            prioSectionNotAvailableFlag[i] = 1;
                                        }
                                        if (prioritySchedule_p[checkSlot] >= eventType)
                                        { //check prio
                                            prioSectionNotdisplaceableFlag[i] = 1;
                                        }
                                        else
                                        {
                                            prioSectiondisplaceablePrio[i] = prioritySchedule_p[checkSlot];
                                        }
                                    }
                                }
                            }
                            for (i = 0; i < roomAmount; i++)
                            { //loop all room for fcfs choose room
                                if ((!fcfsSectionNotAvailableFlag[i]) && (cur_roomSuitable[i] == 1))
                                {
                                    fcfsSelectedRoom = i;
                                    break;
                                }
                            }
                            for (i = 0; i < roomAmount; i++)
                            { //loop all room for prio choose room
                                if ((!prioSectionNotAvailableFlag[i]) && (cur_roomSuitable[i] == 1))
                                {
                                    prioSelectedRoom = i;
                                    break;
                                }
                            }
                            if (prioSelectedRoom < 0)
                            {
                                int minPrio = 255;
                                int min = 255;
                                for (i = 0; i < roomAmount; i++)
                                { //loop all room for prio displace room
                                    if ((!prioSectionNotdisplaceableFlag[i]) && (cur_roomSuitable[i] == 1))
                                    {
                                        if (minPrio > prioSectiondisplaceablePrio[i])
                                        {
                                            minPrio = prioSectiondisplaceablePrio[i];
                                            min = i;
                                        }
                                        //prioSelectedRoom = i;
                                        //break;
                                    }
                                }
                                if (min < 255)
                                {
                                    prioSelectedRoom = min;
                                }
                            }
                            if (fcfsSelectedRoom < 0)
                            {
                                fcfs_rej_flag = 1;
                            }
                            if (prioSelectedRoom < 0)
                            {
                                prio_rej_flag = 1;
                            }
                        }

                        int devicePair = 0; //0 = none, 1 = [projector]+[screen], 2 = [webcam]+[monitor]
                        // check device part pair 1
                        int fcfsSelectedProjector = -1;
                        int prioSelectedProjector = -1;
                        int fcfsSelectedScreen = -1;
                        int prioSelectedScreen = -1;
                        // check device part pair 2
                        int fcfsSelectedWebcam = -1;
                        int prioSelectedWebcam = -1;
                        int fcfsSelectedMonitor = -1;
                        int prioSelectedMonitor = -1;
                        if ((S_numOfWords >= 7) || (eventType == bookDevice))
                        { //plan device, if has device booking

                            char RequestPType[WORD_CHAR_LIMIT];
                            char RequestSType[WORD_CHAR_LIMIT];
                            char RequestWType[WORD_CHAR_LIMIT];
                            char RequestMType[WORD_CHAR_LIMIT];

                            if (eventType != bookDevice)
                            {
                                if (((S_words[6][0] == 'p') && (S_words[7][0] == 's')) || ((S_words[6][0] == 's') && (S_words[7][0] == 'p')))
                                { //[projector]+[screen]
                                    devicePair = 1;
                                    if (S_words[6][0] == 'p')
                                    {
                                        strcpy(RequestPType, S_words[6]);
                                        strcpy(RequestSType, S_words[7]);
                                    }
                                    else
                                    {
                                        strcpy(RequestPType, S_words[7]);
                                        strcpy(RequestSType, S_words[6]);
                                    }
                                }
                                else if (((S_words[6][0] == 'w') && (S_words[7][0] == 'm')) || ((S_words[6][0] == 'm') && (S_words[7][0] == 'w')))
                                { //[webcam]+[monitor]
                                    devicePair = 2;
                                    if (S_words[6][0] == 'w')
                                    {
                                        strcpy(RequestWType, S_words[6]);
                                        strcpy(RequestMType, S_words[7]);
                                    }
                                    else
                                    {
                                        strcpy(RequestWType, S_words[7]);
                                        strcpy(RequestMType, S_words[6]);
                                    }
                                }
                                else
                                {
                                    printf("invalid input at Scheduling module(device not in pair)\n");
                                    break;
                                }
                            }
                            else
                            { //eventType == bookDevice
                                switch (S_words[5][0])
                                {
                                case 'p':
                                    strcpy(RequestPType, S_words[5]);
                                    break;
                                case 's':
                                    strcpy(RequestSType, S_words[5]);
                                    break;
                                case 'w':
                                    strcpy(RequestWType, S_words[5]);
                                    break;
                                case 'm':
                                    strcpy(RequestMType, S_words[5]);
                                    break;
                                default:
                                    printf("no such device\n");
                                    break;
                                }
                            }

                            if ((devicePair == 1) || ((eventType == bookDevice) && (S_words[5][0] == 'p')))
                            { // projector
                                //check if request device correct
                                int cur_pSuitable[projectorAmount] = {[0 ... projectorAmount - 1] = 1};
                                int cur_pSuitableCount = 0;

                                for (i = 0; i < projectorAmount; i++)
                                {
                                    if (strcmp(RequestPType, projectorNames[i]) != 0)
                                    {
                                        cur_pSuitable[i] = 0;
                                    }
                                    else
                                    {
                                        cur_pSuitableCount++;
                                    }
                                }
                                if (cur_pSuitableCount == 0)
                                {
                                    fcfs_rej_flag = 1;
                                    prio_rej_flag = 1;
                                }

                                //check if projector available
                                int fcfsSectionNotAvailableFlag_p[projectorAmount] = {[0 ... projectorAmount - 1] = 0};
                                int prioSectionNotAvailableFlag_p[projectorAmount] = {[0 ... projectorAmount - 1] = 0};
                                int prioSectionNotdisplaceableFlag_p[projectorAmount] = {[0 ... projectorAmount - 1] = 0};
                                int prioSectiondisplaceablePrio_p[projectorAmount] = {[0 ... projectorAmount - 1] = 0};
                                for (i = 0; i < projectorAmount; i++)
                                { //loop all projector
                                    int j;
                                    if (cur_pSuitable[i] == 1)
                                    { // only check the projector with suitable size
                                        fcfsSectionNotAvailableFlag_p[i] = 0;
                                        prioSectionNotAvailableFlag_p[i] = 0;
                                        prioSectionNotdisplaceableFlag_p[i] = 0;
                                        for (j = 0; j < cur_dur; j++)
                                        {
                                            //check projector available
                                            int checkSlot = cur_day_offset + I_projector.offset + (I_projector.length * i) + cur_time + j;
                                            if (fcfsSchedule[checkSlot] != '_')
                                            {
                                                fcfsSectionNotAvailableFlag_p[i] = 1;
                                            }
                                            if (prioritySchedule_p[checkSlot] > 0)
                                            {
                                                prioSectionNotAvailableFlag_p[i] = 1;
                                            }
                                            if (prioritySchedule_p[checkSlot] >= eventType)
                                            { //check prio
                                                prioSectionNotdisplaceableFlag_p[i] = 1;
                                            }
                                            else
                                            {
                                                prioSectiondisplaceablePrio_p[i] = prioritySchedule_p[checkSlot];
                                            }
                                        }
                                    }
                                }
                                for (i = 0; i < projectorAmount; i++)
                                { //loop all projector for fcfs choose projector
                                    if ((!fcfsSectionNotAvailableFlag_p[i]) && (cur_pSuitable[i] == 1))
                                    {
                                        fcfsSelectedProjector = i;
                                        break;
                                    }
                                }
                                for (i = 0; i < projectorAmount; i++)
                                { //loop all projector for prio choose projector
                                    if ((!prioSectionNotAvailableFlag_p[i]) && (cur_pSuitable[i] == 1))
                                    {
                                        prioSelectedProjector = i;
                                        break;
                                    }
                                }
                                if (prioSelectedProjector < 0)
                                {
                                    int minPrio = 255;
                                    int min = 255;
                                    for (i = 0; i < projectorAmount; i++)
                                    { //loop all projector for prio displace projector
                                        if ((!prioSectionNotdisplaceableFlag_p[i]) && (cur_pSuitable[i] == 1))
                                        {
                                            if (minPrio > prioSectiondisplaceablePrio_p[i])
                                            {
                                                minPrio = prioSectiondisplaceablePrio_p[i];
                                                min = i;
                                            }
                                        }
                                    }
                                    if (min < 255)
                                    {
                                        prioSelectedProjector = min;
                                    }
                                }
                                if (fcfsSelectedProjector < 0)
                                {
                                    fcfs_rej_flag = 1;
                                }
                                if (prioSelectedProjector < 0)
                                {
                                    prio_rej_flag = 1;
                                }
                            }

                            if ((devicePair == 1) || ((eventType == bookDevice) && (S_words[5][0] == 's')))
                            { // screen
                                //check if request device correct
                                int cur_sSuitable[screenAmount] = {[0 ... screenAmount - 1] = 1};
                                int cur_sSuitableCount = 0;

                                for (i = 0; i < screenAmount; i++)
                                {
                                    if (strcmp(RequestSType, screenNames[i]) != 0)
                                    {
                                        cur_sSuitable[i] = 0;
                                    }
                                    else
                                    {
                                        cur_sSuitableCount++;
                                    }
                                }
                                if (cur_sSuitableCount == 0)
                                {
                                    fcfs_rej_flag = 1;
                                    prio_rej_flag = 1;
                                }

                                //check if screen available
                                int fcfsSectionNotAvailableFlag_s[screenAmount] = {[0 ... screenAmount - 1] = 0};
                                int prioSectionNotAvailableFlag_s[screenAmount] = {[0 ... screenAmount - 1] = 0};
                                int prioSectionNotdisplaceableFlag_s[screenAmount] = {[0 ... screenAmount - 1] = 0};
                                int prioSectiondisplaceablePrio_s[screenAmount] = {[0 ... screenAmount - 1] = 0};
                                for (i = 0; i < screenAmount; i++)
                                { //loop all screen
                                    int j;
                                    if (cur_sSuitable[i] == 1)
                                    { // only check the screen with suitable size
                                        fcfsSectionNotAvailableFlag_s[i] = 0;
                                        prioSectionNotAvailableFlag_s[i] = 0;
                                        prioSectionNotdisplaceableFlag_s[i] = 0;
                                        for (j = 0; j < cur_dur; j++)
                                        {
                                            //check screen available
                                            int checkSlot = cur_day_offset + I_screen.offset + (I_screen.length * i) + cur_time + j;
                                            if (fcfsSchedule[checkSlot] != '_')
                                            {
                                                fcfsSectionNotAvailableFlag_s[i] = 1;
                                            }
                                            if (prioritySchedule_p[checkSlot] > 0)
                                            {
                                                prioSectionNotAvailableFlag_s[i] = 1;
                                            }
                                            if (prioritySchedule_p[checkSlot] >= eventType)
                                            { //check prio
                                                prioSectionNotdisplaceableFlag_s[i] = 1;
                                            }
                                            else
                                            {
                                                prioSectiondisplaceablePrio_s[i] = prioritySchedule_p[checkSlot];
                                            }
                                        }
                                    }
                                }
                                for (i = 0; i < screenAmount; i++)
                                { //loop all screen for fcfs choose screen
                                    if ((!fcfsSectionNotAvailableFlag_s[i]) && (cur_sSuitable[i] == 1))
                                    {
                                        fcfsSelectedScreen = i;
                                        break;
                                    }
                                }
                                for (i = 0; i < screenAmount; i++)
                                { //loop all screen for prio choose screen
                                    if ((!prioSectionNotAvailableFlag_s[i]) && (cur_sSuitable[i] == 1))
                                    {
                                        prioSelectedScreen = i;
                                        break;
                                    }
                                }
                                if (prioSelectedScreen < 0)
                                {
                                    int minPrio = 255;
                                    int min = 255;
                                    for (i = 0; i < screenAmount; i++)
                                    { //loop all screen for prio displace screen
                                        if ((!prioSectionNotdisplaceableFlag_s[i]) && (cur_sSuitable[i] == 1))
                                        {
                                            if (minPrio > prioSectiondisplaceablePrio_s[i])
                                            {
                                                minPrio = prioSectiondisplaceablePrio_s[i];
                                                min = i;
                                            }
                                        }
                                    }
                                    if (min < 255)
                                    {
                                        prioSelectedScreen = min;
                                    }
                                }
                                if (fcfsSelectedScreen < 0)
                                {
                                    fcfs_rej_flag = 1;
                                }
                                if (prioSelectedScreen < 0)
                                {
                                    prio_rej_flag = 1;
                                }
                            }

                            if ((devicePair == 2) || ((eventType == bookDevice) && (S_words[5][0] == 'w')))
                            { // webcam
                                //check if request device correct
                                int cur_wSuitable[webcamAmount] = {[0 ... webcamAmount - 1] = 1};
                                int cur_wSuitableCount = 0;

                                for (i = 0; i < webcamAmount; i++)
                                {
                                    if (strcmp(RequestWType, webcamNames[i]) != 0)
                                    {
                                        cur_wSuitable[i] = 0;
                                    }
                                    else
                                    {
                                        cur_wSuitableCount++;
                                    }
                                }
                                if (cur_wSuitableCount == 0)
                                {
                                    fcfs_rej_flag = 1;
                                    prio_rej_flag = 1;
                                }

                                //check if webcam available
                                int fcfsSectionNotAvailableFlag_w[webcamAmount] = {[0 ... webcamAmount - 1] = 0};
                                int prioSectionNotAvailableFlag_w[webcamAmount] = {[0 ... webcamAmount - 1] = 0};
                                int prioSectionNotdisplaceableFlag_w[webcamAmount] = {[0 ... webcamAmount - 1] = 0};
                                int prioSectiondisplaceablePrio_w[webcamAmount] = {[0 ... webcamAmount - 1] = 0};
                                for (i = 0; i < webcamAmount; i++)
                                { //loop all webcam
                                    int j;
                                    if (cur_wSuitable[i] == 1)
                                    { // only check the webcam with suitable size
                                        fcfsSectionNotAvailableFlag_w[i] = 0;
                                        prioSectionNotAvailableFlag_w[i] = 0;
                                        prioSectionNotdisplaceableFlag_w[i] = 0;
                                        for (j = 0; j < cur_dur; j++)
                                        {
                                            //check webcam available
                                            int checkSlot = cur_day_offset + I_webcam.offset + (I_webcam.length * i) + cur_time + j;
                                            if (fcfsSchedule[checkSlot] != '_')
                                            {
                                                fcfsSectionNotAvailableFlag_w[i] = 1;
                                            }
                                            if (prioritySchedule_p[checkSlot] > 0)
                                            {
                                                prioSectionNotAvailableFlag_w[i] = 1;
                                            }
                                            if (prioritySchedule_p[checkSlot] >= eventType)
                                            { //check prio
                                                prioSectionNotdisplaceableFlag_w[i] = 1;
                                            }
                                            else
                                            {
                                                prioSectiondisplaceablePrio_w[i] = prioritySchedule_p[checkSlot];
                                            }
                                        }
                                    }
                                }
                                for (i = 0; i < webcamAmount; i++)
                                { //loop all webcam for fcfs choose webcam
                                    if ((!fcfsSectionNotAvailableFlag_w[i]) && (cur_wSuitable[i] == 1))
                                    {
                                        fcfsSelectedWebcam = i;
                                        break;
                                    }
                                }
                                for (i = 0; i < webcamAmount; i++)
                                { //loop all webcam for prio choose webcam
                                    if ((!prioSectionNotAvailableFlag_w[i]) && (cur_wSuitable[i] == 1))
                                    {
                                        prioSelectedWebcam = i;
                                        break;
                                    }
                                }
                                if (prioSelectedWebcam < 0)
                                {
                                    int minPrio = 255;
                                    int min = 255;
                                    for (i = 0; i < webcamAmount; i++)
                                    { //loop all webcam for prio displace webcam
                                        if ((!prioSectionNotdisplaceableFlag_w[i]) && (cur_wSuitable[i] == 1))
                                        {
                                            if (minPrio > prioSectiondisplaceablePrio_w[i])
                                            {
                                                minPrio = prioSectiondisplaceablePrio_w[i];
                                                min = i;
                                            }
                                        }
                                    }
                                    if (min < 255)
                                    {
                                        prioSelectedWebcam = min;
                                    }
                                }
                                if (fcfsSelectedWebcam < 0)
                                {
                                    fcfs_rej_flag = 1;
                                }
                                if (prioSelectedWebcam < 0)
                                {
                                    prio_rej_flag = 1;
                                }
                            }

                            if ((devicePair == 2) || ((eventType == bookDevice) && (S_words[5][0] == 'm')))
                            { // monitor
                                //check if request device correct
                                int cur_mSuitable[monitorAmount] = {[0 ... monitorAmount - 1] = 1};
                                int cur_mSuitableCount = 0;

                                for (i = 0; i < monitorAmount; i++)
                                {
                                    if (strcmp(RequestMType, monitorNames[i]) != 0)
                                    {
                                        cur_mSuitable[i] = 0;
                                    }
                                    else
                                    {
                                        cur_mSuitableCount++;
                                    }
                                }
                                if (cur_mSuitableCount == 0)
                                {
                                    fcfs_rej_flag = 1;
                                    prio_rej_flag = 1;
                                }

                                //check if monitor available
                                int fcfsSectionNotAvailableFlag_m[monitorAmount] = {[0 ... monitorAmount - 1] = 0};
                                int prioSectionNotAvailableFlag_m[monitorAmount] = {[0 ... monitorAmount - 1] = 0};
                                int prioSectionNotdisplaceableFlag_m[monitorAmount] = {[0 ... monitorAmount - 1] = 0};
                                int prioSectiondisplaceablePrio_m[monitorAmount] = {[0 ... monitorAmount - 1] = 0};
                                for (i = 0; i < monitorAmount; i++)
                                { //loop all monitor
                                    int j;
                                    if (cur_mSuitable[i] == 1)
                                    { // only check the monitor with suitable size
                                        fcfsSectionNotAvailableFlag_m[i] = 0;
                                        prioSectionNotAvailableFlag_m[i] = 0;
                                        prioSectionNotdisplaceableFlag_m[i] = 0;
                                        for (j = 0; j < cur_dur; j++)
                                        {
                                            //check monitor available
                                            int checkSlot = cur_day_offset + I_monitor.offset + (I_monitor.length * i) + cur_time + j;
                                            if (fcfsSchedule[checkSlot] != '_')
                                            {
                                                fcfsSectionNotAvailableFlag_m[i] = 1;
                                            }
                                            if (prioritySchedule_p[checkSlot] > 0)
                                            {
                                                prioSectionNotAvailableFlag_m[i] = 1;
                                            }
                                            if (prioritySchedule_p[checkSlot] >= eventType)
                                            { //check prio
                                                prioSectionNotdisplaceableFlag_m[i] = 1;
                                            }
                                            else
                                            {
                                                prioSectiondisplaceablePrio_m[i] = prioritySchedule_p[checkSlot];
                                            }
                                        }
                                    }
                                }
                                for (i = 0; i < monitorAmount; i++)
                                { //loop all monitor for fcfs choose monitor
                                    if ((!fcfsSectionNotAvailableFlag_m[i]) && (cur_mSuitable[i] == 1))
                                    {
                                        fcfsSelectedMonitor = i;
                                        break;
                                    }
                                }
                                for (i = 0; i < monitorAmount; i++)
                                { //loop all monitor for prio choose monitor
                                    if ((!prioSectionNotAvailableFlag_m[i]) && (cur_mSuitable[i] == 1))
                                    {
                                        prioSelectedMonitor = i;
                                        break;
                                    }
                                }
                                if (prioSelectedMonitor < 0)
                                {
                                    int minPrio = 255;
                                    int min = 255;
                                    for (i = 0; i < monitorAmount; i++)
                                    { //loop all monitor for prio displace monitor
                                        if ((!prioSectionNotdisplaceableFlag_m[i]) && (cur_mSuitable[i] == 1))
                                        {
                                            if (minPrio > prioSectiondisplaceablePrio_m[i])
                                            {
                                                minPrio = prioSectiondisplaceablePrio_m[i];
                                                min = i;
                                            }
                                        }
                                    }
                                    if (min < 255)
                                    {
                                        prioSelectedMonitor = min;
                                    }
                                }
                                if (fcfsSelectedMonitor < 0)
                                {
                                    fcfs_rej_flag = 1;
                                }
                                if (prioSelectedMonitor < 0)
                                {
                                    prio_rej_flag = 1;
                                }
                            }
                        }

                        if (fcfs_rej_flag)
                        { //if this command rejected
                            fcfsRejectedCommandIndex[fcfsRejectedCount] = S_CommandIndex;
                            fcfsRejectedCount++;
                        }
                        else
                        { //this command accepted

                            char tenant_ID = '_';
                            switch (S_words[1][8])
                            {
                            case 'A':
                                tenant_ID = '0';
                                break;
                            case 'B':
                                tenant_ID = '1';
                                break;
                            case 'C':
                                tenant_ID = '2';
                                break;
                            case 'D':
                                tenant_ID = '3';
                                break;
                            case 'E':
                                tenant_ID = '4';
                                break;
                            }

                            if ((eventType != none) && (eventType != bookDevice))
                            { //insert room
                                int j;
                                for (j = 0; j < cur_dur; j++)
                                {
                                    int insertSlot = cur_day_offset + I_room.offset + (I_room.length * fcfsSelectedRoom) + cur_time + j;
                                    if ((cur_time + j) > timeSlotSize)
                                    {
                                        insertSlot = insertSlot + day_Length;
                                    };
                                    fcfsSchedule[insertSlot] = tenant_ID;
                                }
                            }

                            if ((S_numOfWords > 5) || (eventType == bookDevice))
                            { //insert device
                                int j;
                                if ((devicePair == 1) || ((eventType == bookDevice) && (S_words[5][0] == 'p')))
                                { //[projector]
                                    for (j = 0; j < cur_dur; j++)
                                    {
                                        int insertSlot_p = cur_day_offset + I_projector.offset + (I_projector.length * fcfsSelectedProjector) + cur_time + j;
                                        if ((cur_time + j) > timeSlotSize)
                                        {
                                            insertSlot_p = insertSlot_p + day_Length;
                                        };
                                        fcfsSchedule[insertSlot_p] = tenant_ID;
                                    }
                                }
                                if ((devicePair == 1) || ((eventType == bookDevice) && (S_words[5][0] == 's')))
                                { //[screen]
                                    for (j = 0; j < cur_dur; j++)
                                    {
                                        int insertSlot_s = cur_day_offset + I_screen.offset + (I_screen.length * fcfsSelectedScreen) + cur_time + j;
                                        if ((cur_time + j) > timeSlotSize)
                                        {
                                            insertSlot_s = insertSlot_s + day_Length;
                                        };
                                        fcfsSchedule[insertSlot_s] = tenant_ID;
                                    }
                                }

                                if ((devicePair == 2) || ((eventType == bookDevice) && (S_words[5][0] == 'w')))
                                { //[webcam]
                                    for (j = 0; j < cur_dur; j++)
                                    {
                                        int insertSlot_w = cur_day_offset + I_webcam.offset + (I_webcam.length * fcfsSelectedWebcam) + cur_time + j;
                                        if ((cur_time + j) > timeSlotSize)
                                        {
                                            insertSlot_w = insertSlot_w + day_Length;
                                        };
                                        fcfsSchedule[insertSlot_w] = tenant_ID;
                                    }
                                }
                                if ((devicePair == 2) || ((eventType == bookDevice) && (S_words[5][0] == 'm')))
                                { //[monitor]
                                    for (j = 0; j < cur_dur; j++)
                                    {
                                        int insertSlot_m = cur_day_offset + I_monitor.offset + (I_monitor.length * fcfsSelectedMonitor) + cur_time + j;
                                        if ((cur_time + j) > timeSlotSize)
                                        {
                                            insertSlot_m = insertSlot_m + day_Length;
                                        };
                                        fcfsSchedule[insertSlot_m] = tenant_ID;
                                    }
                                }
                            }
                        }
                        if (prio_rej_flag)
                        { //if this command rejected
                            priorityRejectedCommandIndex[priorityRejectedCount] = S_CommandIndex;
                            priorityRejectedCount++;
                        }
                        else
                        { //this command accepted

                            char tenant_ID = '_';
                            switch (S_words[1][8])
                            {
                            case 'A':
                                tenant_ID = '0';
                                break;
                            case 'B':
                                tenant_ID = '1';
                                break;
                            case 'C':
                                tenant_ID = '2';
                                break;
                            case 'D':
                                tenant_ID = '3';
                                break;
                            case 'E':
                                tenant_ID = '4';
                                break;
                            }

                            if ((eventType != none) && (eventType != bookDevice))
                            { //insert room
                                int j, k, l;
                                for (j = 0; j < cur_dur; j++)
                                {

                                    int insertSlot = cur_day_offset + I_room.offset + (I_room.length * prioSelectedRoom) + cur_time + j;
                                    if ((cur_time + j) > timeSlotSize)
                                    {
                                        insertSlot = insertSlot + day_Length;
                                    };
                                    if (prioritySchedule_c[insertSlot] >= 0)
                                    { //reject the lower prio event
                                        int rejectedCmd = prioritySchedule_c[insertSlot];
                                        priorityRejectedCommandIndex[priorityRejectedCount] = rejectedCmd;
                                        priorityRejectedCount++;
                                        for (k = 0; k < 7; k++)
                                        {
                                            for (l = date_Length; l < day_Length; l++)
                                                if (prioritySchedule_c[k * day_Length + l] == rejectedCmd)
                                                {
                                                    prioritySchedule[k * day_Length + l] = '_';
                                                    prioritySchedule_p[k * day_Length + l] = none;
                                                    prioritySchedule_c[k * day_Length + l] = -1;
                                                }
                                        }
                                    }
                                    prioritySchedule[insertSlot] = tenant_ID;
                                    prioritySchedule_p[insertSlot] = eventType;
                                    prioritySchedule_c[insertSlot] = S_CommandIndex;
                                }
                            }

                            if ((S_numOfWords > 5) || (eventType == bookDevice))
                            { //insert device
                                if ((devicePair == 1) || ((eventType == bookDevice) && (S_words[5][0] == 'p')))
                                { //[projector]
                                    int j, k, l;
                                    for (j = 0; j < cur_dur; j++)
                                    {
                                        int insertSlot_p = cur_day_offset + I_projector.offset + (I_projector.length * prioSelectedProjector) + cur_time + j;
                                        if ((cur_time + j) > timeSlotSize)
                                        {
                                            insertSlot_p = insertSlot_p + day_Length;
                                        };
                                        if (prioritySchedule_c[insertSlot_p] > 0)
                                        { //reject the lower prio event
                                            int rejectedCmd = prioritySchedule_c[insertSlot_p];
                                            priorityRejectedCommandIndex[priorityRejectedCount] = rejectedCmd;
                                            priorityRejectedCount++;
                                            for (k = 0; k < 7; k++)
                                            {
                                                for (l = date_Length; l < day_Length; l++)
                                                    if (prioritySchedule_c[k * day_Length + l] == rejectedCmd)
                                                    {
                                                        prioritySchedule[k * day_Length + l] = '_';
                                                        prioritySchedule_p[k * day_Length + l] = none;
                                                        prioritySchedule_c[k * day_Length + l] = -1;
                                                    }
                                            }
                                        }
                                        prioritySchedule[insertSlot_p] = tenant_ID;
                                        prioritySchedule_p[insertSlot_p] = eventType;
                                        prioritySchedule_c[insertSlot_p] = S_CommandIndex;
                                    }
                                }
                                if ((devicePair == 1) || ((eventType == bookDevice) && (S_words[5][0] == 's')))
                                { //[screen]
                                    int j, k, l;
                                    for (j = 0; j < cur_dur; j++)
                                    {
                                        int insertSlot_s = cur_day_offset + I_screen.offset + (I_screen.length * prioSelectedScreen) + cur_time + j;
                                        if ((cur_time + j) > timeSlotSize)
                                        {
                                            insertSlot_s = insertSlot_s + day_Length;
                                        };
                                        if (prioritySchedule_c[insertSlot_s] >= 0)
                                        { //reject the lower prio event
                                            int rejectedCmd = prioritySchedule_c[insertSlot_s];
                                            priorityRejectedCommandIndex[priorityRejectedCount] = rejectedCmd;
                                            priorityRejectedCount++;
                                            for (k = 0; k < 7; k++)
                                            {
                                                for (l = date_Length; l < day_Length; l++)
                                                    if (prioritySchedule_c[k * day_Length + l] == rejectedCmd)
                                                    {
                                                        prioritySchedule[k * day_Length + l] = '_';
                                                        prioritySchedule_p[k * day_Length + l] = none;
                                                        prioritySchedule_c[k * day_Length + l] = -1;
                                                    }
                                            }
                                        }
                                        prioritySchedule[insertSlot_s] = tenant_ID;
                                        prioritySchedule_p[insertSlot_s] = eventType;
                                        prioritySchedule_c[insertSlot_s] = S_CommandIndex;
                                    }
                                }

                                if ((devicePair == 2) || ((eventType == bookDevice) && (S_words[5][0] == 'w')))
                                { //[webcam]
                                    int j, k, l;
                                    for (j = 0; j < cur_dur; j++)
                                    {
                                        int insertSlot_w = cur_day_offset + I_webcam.offset + (I_webcam.length * prioSelectedWebcam) + cur_time + j;
                                        if ((cur_time + j) > timeSlotSize)
                                        {
                                            insertSlot_w = insertSlot_w + day_Length;
                                        };
                                        if (prioritySchedule_c[insertSlot_w] >= 0)
                                        { //reject the lower prio event
                                            int rejectedCmd = prioritySchedule_c[insertSlot_w];
                                            priorityRejectedCommandIndex[priorityRejectedCount] = rejectedCmd;
                                            priorityRejectedCount++;
                                            for (k = 0; k < 7; k++)
                                            {
                                                for (l = date_Length; l < day_Length; l++)
                                                    if (prioritySchedule_c[k * day_Length + l] == rejectedCmd)
                                                    {
                                                        prioritySchedule[k * day_Length + l] = '_';
                                                        prioritySchedule_p[k * day_Length + l] = none;
                                                        prioritySchedule_c[k * day_Length + l] = -1;
                                                    }
                                            }
                                        }
                                        prioritySchedule[insertSlot_w] = tenant_ID;
                                        prioritySchedule_p[insertSlot_w] = eventType;
                                        prioritySchedule_c[insertSlot_w] = S_CommandIndex;
                                    }
                                }
                                if ((devicePair == 2) || ((eventType == bookDevice) && (S_words[5][0] == 'm')))
                                { //[monitor]
                                    int j, k, l;
                                    for (j = 0; j < cur_dur; j++)
                                    {
                                        int insertSlot_m = cur_day_offset + I_monitor.offset + (I_monitor.length * prioSelectedMonitor) + cur_time + j;
                                        if ((cur_time + j) > timeSlotSize)
                                        {
                                            insertSlot_m = insertSlot_m + day_Length;
                                        };
                                        if (prioritySchedule_c[insertSlot_m] >= 0)
                                        { //reject the lower prio event
                                            int rejectedCmd = prioritySchedule_c[insertSlot_m];
                                            priorityRejectedCommandIndex[priorityRejectedCount] = rejectedCmd;
                                            priorityRejectedCount++;
                                            for (k = 0; k < 7; k++)
                                            {
                                                for (l = date_Length; l < day_Length; l++)
                                                    if (prioritySchedule_c[k * day_Length + l] == rejectedCmd)
                                                    {
                                                        prioritySchedule[k * day_Length + l] = '_';
                                                        prioritySchedule_p[k * day_Length + l] = none;
                                                        prioritySchedule_c[k * day_Length + l] = -1;
                                                    }
                                            }
                                        }
                                        prioritySchedule[insertSlot_m] = tenant_ID;
                                        prioritySchedule_p[insertSlot_m] = eventType;
                                        prioritySchedule_c[insertSlot_m] = S_CommandIndex;
                                    }
                                }
                            }
                        }
                    }

                    if (strcmp(mode, "-fcfs") == 0)
                    {
                        write(schedulePipe[1][1], fcfsSchedule, SCHEDULE_STRING_LENGTH);
                        int length = snprintf(NULL, 0, "%d", fcfsRejectedCount);
                        char *str = malloc(length + 1);
                        snprintf(str, length + 1, "%d", fcfsRejectedCount);
                        write(schedulePipe[1][1], str, COMMAND_CHAR_SIZE);
                        free(str);
                        for (i = 0; i < fcfsRejectedCount; i++)
                        {
                            length = snprintf(NULL, 0, "%d", fcfsRejectedCommandIndex[i]);
                            str = malloc(length + 1);
                            snprintf(str, length + 1, "%d", fcfsRejectedCommandIndex[i]);
                            write(schedulePipe[1][1], str, COMMAND_CHAR_SIZE);
                            free(str);
                        }
                    }
                    else if (strcmp(mode, "-prio") == 0)
                    {
                        write(schedulePipe[1][1], prioritySchedule, SCHEDULE_STRING_LENGTH);
                        int length = snprintf(NULL, 0, "%d", priorityRejectedCount);
                        char *str = malloc(length + 1);
                        snprintf(str, length + 1, "%d", priorityRejectedCount);
                        write(schedulePipe[1][1], str, COMMAND_CHAR_SIZE);
                        free(str);
                        for (i = 0; i < priorityRejectedCount; i++)
                        {
                            length = snprintf(NULL, 0, "%d", priorityRejectedCommandIndex[i]);
                            str = malloc(length + 1);
                            snprintf(str, length + 1, "%d", priorityRejectedCommandIndex[i]);
                            write(schedulePipe[1][1], str, COMMAND_CHAR_SIZE);
                            free(str);
                        }
                    }
                    else if (strcmp(mode, "-ALL") == 0)
                    {
                        write(schedulePipe[1][1], fcfsSchedule, SCHEDULE_STRING_LENGTH);
                        write(schedulePipe[1][1], prioritySchedule, SCHEDULE_STRING_LENGTH);
                        int length = snprintf(NULL, 0, "%d", fcfsRejectedCount);
                        char *str = malloc(length + 1);
                        snprintf(str, length + 1, "%d", fcfsRejectedCount);
                        write(schedulePipe[1][1], str, COMMAND_CHAR_SIZE);
                        free(str);
                        for (i = 0; i < fcfsRejectedCount; i++)
                        {
                            length = snprintf(NULL, 0, "%d", fcfsRejectedCommandIndex[i]);
                            str = malloc(length + 1);
                            snprintf(str, length + 1, "%d", fcfsRejectedCommandIndex[i]);
                            write(schedulePipe[1][1], str, COMMAND_CHAR_SIZE);
                            free(str);
                        }
                        write(schedulePipe[1][1], "&&", COMMAND_CHAR_SIZE);
                        length = snprintf(NULL, 0, "%d", priorityRejectedCount);
                        str = malloc(length + 1);
                        snprintf(str, length + 1, "%d", priorityRejectedCount);
                        write(schedulePipe[1][1], str, COMMAND_CHAR_SIZE);
                        free(str);
                        for (i = 0; i < priorityRejectedCount; i++)
                        {
                            length = snprintf(NULL, 0, "%d", priorityRejectedCommandIndex[i]);
                            str = malloc(length + 1);
                            snprintf(str, length + 1, "%d", priorityRejectedCommandIndex[i]);
                            write(schedulePipe[1][1], str, COMMAND_CHAR_SIZE);
                            free(str);
                        }
                    }
                    isFirstLine = 1;
                    numOfCommand = 0;
                }
            }

            close(schedulePipe[1][1]);
            close(schedulePipe[0][0]);
            // printf("Schedule Module: done\n");
        }
        else
        {
            // Parent
            close(schedulePipe[1][1]);
            close(schedulePipe[0][0]);
            // STEP 3: fork/pipe OUTPUT MODULE
            int outputPipe[2][2];
            pid_t outputCid;
            // create pipe, parent use [0][1] write, child use [1][1] write
            for (i = 0; i < 2; i++)
            {
                if (pipe(outputPipe[i]) < 0)
                {
                    printf("Error: output pipe creation error\n");
                    exit(1);
                }
            }
            if ((outputCid = fork()) < 0)
            {
                printf("Error: fork output module error\n");
                exit(1);
            }
            else if (outputCid == 0)
            {
                // TODO: Output module (part 3)
                close(inputPipe[1][0]);
                close(inputPipe[0][1]);
                close(schedulePipe[0][1]);
                close(schedulePipe[1][0]);
                close(outputPipe[1][0]);
                close(outputPipe[0][1]);
                // printf("Output Module: started.\n");

                int n;
                int isFirstLine = 1;
                char buf[COMMAND_CHAR_SIZE];
                char fcfsSchedule[SCHEDULE_STRING_LENGTH + 1];
                char prioSchedule[SCHEDULE_STRING_LENGTH + 1];

                char mode[10];
                char commandHistory[MAX_COMMAND][COMMAND_CHAR_SIZE];
                int numOfCommand = 0;

                while (1)
                {
                    n = read(outputPipe[0][0], buf, COMMAND_CHAR_SIZE);
                    buf[n] = 0;
                    // printf("Output module: received [%s]\n", buf);
                    if (strcmp(buf, "CLOSE") == 0)
                    {
                        // printf("output module: closing...\n");
                        break;
                    }
                    else if (strcmp(buf, "END") == 0)
                    {
                        // -- Loging the data received from parent
                        // printf("Ouput: num of command = %d\n", numOfCommand);
                        // printf("Ouput: fcfs rejected amount = %d\n", fcfsRejectedCount);
                        // printf("Ouput: num of command = %d\n", priorityRejectedCount);
                        // printf("Output: command history:\n");
                        // for (i = 0; i < numOfCommand; i++)
                        // {
                        //     printf("[%s]\n", commandHistory[i]);
                        // }
                        // printf("Output: fcfs rejected index: [");
                        // for (i = 0; i < fcfsRejectedCount; i++)
                        // {
                        //     printf("%d, ", fcfsRejectedCommandIndex[i]);
                        // }
                        // printf("]\n");
                        // printf("Output: priority rejected index: [");
                        // for (i = 0; i < priorityRejectedCount; i++)
                        // {
                        //     printf("%d, ", priorityRejectedCommandIndex[i]);
                        // }
                        // printf("]\n");
                        // printf("Output: fcfs Schedule = %s\n", fcfsSchedule);
                        // printf("Output: prio schedule = %s\n", prioSchedule);

                        int printFCFS = 0;
                        int printPRIO = 0;

                        if (strcmp(mode, "-fcfs") == 0)
                        {
                            printFCFS = 1;
                        }
                        else if (strcmp(mode, "-prio") == 0)
                        {
                            printPRIO = 1;
                        }
                        else if (strcmp(mode, "-ALL") == 0)
                        {
                            printFCFS = 1;
                            printPRIO = 1;
                        }

                        // print to file
                        FILE *fp;
                        char *currentUnixTime = getStringFromInt(time(NULL));
                        char fileName[] = DIRECTORY_OUTPUT_PREFIX;
                        strcat(fileName, currentUnixTime);
                        strcat(fileName, ".txt");
                        printf(" -> [output file: %s]\n", fileName);
                        fp = fopen(fileName, "w");
                        free(currentUnixTime);
                        if (fp == NULL)
                        {
                            printf("Output module Error, error in opening\n");
                            // end
                            fcfsRejectedCount = 0;
                            priorityRejectedCount = 0;
                            numOfCommand = 0;
                            fcfsSchedule[0] = 0;
                            prioSchedule[0] = 0;
                            continue;
                        }

                        int u, h, deviceIndexStart, rejected, endtime, duration, numOfWords, startTimeInt, needFindRoom, participant, isNextDay;
                        int oneDayScheduleSize = (roomAmount + webcamAmount + monitorAmount + projectorAmount + screenAmount) * 24 + 10;
                        char startTime[3];
                        char room[20];
                        char tempDate[11];
                        char tempSchedule[25];
                        char *endTimeString;
                        char allDeviceString[100];
                        if (printFCFS == 1)
                        {
                            // print all accepted bookings
                            fprintf(fp, "*** Room Booking - ACCEPTED / FCFS ***\n");
                            for (i = 0; i < tenantAmount; i++)
                            {
                                fprintf(fp, "%s has the following bookings:\n", tenantNames[i]);
                                fprintf(fp, "Date        Start  End                  Type         Room     Device           \n");
                                fprintf(fp, "=================================================================\n");
                                for (u = 0; u < numOfCommand; u++)
                                {
                                    strcpy(room, "XXX");
                                    rejected = 0;
                                    for (h = 0; h < fcfsRejectedCount; h++)
                                    {
                                        if (fcfsRejectedCommandIndex[h] == u)
                                        {
                                            rejected = 1;
                                            break;
                                        }
                                    }
                                    if (rejected == 0)
                                    {
                                        numOfWords = getNumOfWord(commandHistory[u], strlen(commandHistory[u]));
                                        char **words = splitCommand(commandHistory[u], strlen(commandHistory[u]));
                                        words[1]++;
                                        // for (i = 0; i < numOfWords; i++) {
                                        //     printf("Output: word[%d] = %s\n", i, words[i]);
                                        // }
                                        startTime[0] = words[3][0];
                                        startTime[1] = words[3][1];
                                        startTime[2] = 0;
                                        duration = getIntFromString(words[4]);
                                        //printf("Output: duration of [%s] is %d\n", words[4], duration);
                                        startTimeInt = getIntFromString(startTime);
                                        //printf("Output: start time of [%s] is %d\n", startTime, startTimeInt);
                                        endtime = getIntFromString(startTime) + duration;
                                        if (endtime > 23)
                                        {
                                            isNextDay = 1;
                                            endtime = endtime - 24;
                                        }
                                        else
                                        {
                                            isNextDay = 0;
                                        }
                                        //printf("Output: end time is %d\n", endtime);
                                        endTimeString = getStringFromInt(endtime);
                                        if (strcmp(words[0], "bookDevice") == 0)
                                        {
                                            needFindRoom = 0;
                                            deviceIndexStart = 5;
                                            strcpy(words[0], "*");
                                        }
                                        else
                                        {
                                            needFindRoom = 1;
                                            deviceIndexStart = 6;
                                            words[0] = words[0] + 3;
                                        }
                                        // format devices string
                                        allDeviceString[0] = 0;
                                        for (h = deviceIndexStart; h < numOfWords; h++)
                                        {
                                            strcat(allDeviceString, words[h]);
                                            strcat(allDeviceString, " ");
                                        }

                                        // finding the room
                                        // search for each room (with suitable room size)
                                        // check if the time slot (from start to end) is occupied by the tanent
                                        h = 0;
                                        int r, q, isOnThatDay;
                                        participant = getIntFromString(words[5]);
                                        if (needFindRoom == 1)
                                        {
                                            // printf("Output: checking rooms (fcfs) for command %d\n", u);
                                            while (h < SCHEDULE_STRING_LENGTH)
                                            {
                                                memcpy(tempDate, &fcfsSchedule[h], 10);
                                                tempDate[10] = '\0';
                                                // check if date is the same
                                                //printf("Output: checking date: %s vs %s\n", tempDate, words[2]);
                                                if (strcmp(tempDate, words[2]) == 0)
                                                {
                                                    // find each room and check availability
                                                    //printf("Output: found same date: %s\n", tempDate);
                                                    for (r = 0; r < roomAmount; r++)
                                                    {
                                                        if (roomCapacity[r] >= participant)
                                                        {
                                                            //printf("Output: found room with good size: %s\n", roomNames[r]);
                                                            int tempStart = h + 10 + r * 24 + startTimeInt;
                                                            isOnThatDay = 1;
                                                            char t = i + '0';
                                                            for (q = 0; q < duration; q++)
                                                            {
                                                                if (fcfsSchedule[tempStart + q] != t)
                                                                {
                                                                    isOnThatDay = 0;
                                                                    break;
                                                                }
                                                            }
                                                            if (isOnThatDay == 1)
                                                            {
                                                                strcpy(room, roomNames[r]);
                                                                break;
                                                            }
                                                        }
                                                    }
                                                }
                                                h = h + oneDayScheduleSize;
                                            }
                                        }
                                        else
                                        {
                                            strcpy(room, "*");
                                        }

                                        if (strcmp(words[1], tenantNames[i]) == 0)
                                        {
                                            if (isNextDay == 0)
                                            {
                                                fprintf(fp, "%-12s%-7s%2s:00                %-13s%-9s%s\n", words[2], words[3], endTimeString, words[0], room, allDeviceString);
                                            }
                                            else
                                            {
                                                fprintf(fp, "%-12s%-7s%2s:00(the next day)  %-13s%-9s%s\n", words[2], words[3], endTimeString, words[0], room, allDeviceString);
                                            }
                                        }
                                        free(words);
                                    }
                                }
                                fprintf(fp, "\n");
                            }
                            fprintf(fp, "\n");

                            // print all rejected command
                            fprintf(fp, "*** Room Booking - REJECTED / FCFS ***\n");
                            for (i = 0; i < tenantAmount; i++)
                            {
                                fprintf(fp, "%s has the following bookings:\n", tenantNames[i]);
                                fprintf(fp, "Date        Start  End                  Type         Device           \n");
                                fprintf(fp, "=================================================================\n");
                                for (u = 0; u < fcfsRejectedCount; u++)
                                {
                                    char currentCmd[COMMAND_CHAR_SIZE];
                                    strcpy(currentCmd, commandHistory[fcfsRejectedCommandIndex[u]]);
                                    numOfWords = getNumOfWord(currentCmd, strlen(currentCmd));
                                    char **words = splitCommand(currentCmd, strlen(currentCmd));
                                    words[1]++;
                                    //printf("Output: comparing %s to %s\n", words[1], tenantNames[i]);
                                    if (strcmp(words[1], tenantNames[i]) == 0)
                                    {
                                        startTime[0] = words[3][0];
                                        startTime[1] = words[3][1];
                                        startTime[2] = 0;
                                        duration = getIntFromString(words[4]);
                                        //printf("Output: duration of [%s] is %d\n", words[4], duration);
                                        startTimeInt = getIntFromString(startTime);
                                        //printf("Output: start time of [%s] is %d\n", startTime, startTimeInt);
                                        endtime = getIntFromString(startTime) + duration;
                                        if (endtime > 23)
                                        {
                                            isNextDay = 1;
                                            endtime = endtime - 24;
                                        }
                                        else
                                        {
                                            isNextDay = 0;
                                        }
                                        //printf("Output: end time is %d\n", endtime);
                                        endTimeString = getStringFromInt(endtime);
                                        if (strcmp(words[0], "bookDevice") == 0)
                                        {
                                            deviceIndexStart = 5;
                                            strcpy(words[0], "*");
                                        }
                                        else
                                        {
                                            deviceIndexStart = 6;
                                            words[0] = words[0] + 3;
                                        }
                                        allDeviceString[0] = 0;
                                        for (h = deviceIndexStart; h < numOfWords; h++)
                                        {
                                            strcat(allDeviceString, words[h]);
                                            strcat(allDeviceString, " ");
                                        }
                                        if (isNextDay == 0)
                                        {
                                            fprintf(fp, "%-12s%-7s%2s:00                %-13s%s\n", words[2], words[3], endTimeString, words[0], allDeviceString);
                                        }
                                        else 
                                        {
                                            fprintf(fp, "%-12s%-7s%2s:00(the next day)  %-13s%s\n", words[2], words[3], endTimeString, words[0], allDeviceString);
                                        }
                                    }
                                    free(words);
                                }
                                fprintf(fp, "\n");
                            }
                            fprintf(fp, "\n\n");
                        }

                        if (printPRIO == 1)
                        {
                            // print all accepted bookings
                            fprintf(fp, "*** Room Booking - ACCEPTED / Priority ***\n");
                            for (i = 0; i < tenantAmount; i++)
                            {
                                fprintf(fp, "%s has the following bookings:\n", tenantNames[i]);
                                fprintf(fp, "Date        Start  End                  Type         Room     Device           \n");
                                fprintf(fp, "=================================================================\n");
                                for (u = 0; u < numOfCommand; u++)
                                {
                                    strcpy(room, "XXX");
                                    rejected = 0;
                                    for (h = 0; h < priorityRejectedCount; h++)
                                    {
                                        if (priorityRejectedCommandIndex[h] == u)
                                        {
                                            rejected = 1;
                                            break;
                                        }
                                    }
                                    if (rejected == 0)
                                    {
                                        numOfWords = getNumOfWord(commandHistory[u], strlen(commandHistory[u]));
                                        char **words = splitCommand(commandHistory[u], strlen(commandHistory[u]));
                                        words[1]++;
                                        // for (i = 0; i < numOfWords; i++) {
                                        //     printf("Output: word[%d] = %s\n", i, words[i]);
                                        // }
                                        startTime[0] = words[3][0];
                                        startTime[1] = words[3][1];
                                        startTime[2] = 0;
                                        duration = getIntFromString(words[4]);
                                        //printf("Output: duration of [%s] is %d\n", words[4], duration);
                                        startTimeInt = getIntFromString(startTime);
                                        //printf("Output: start time of [%s] is %d\n", startTime, startTimeInt);
                                        endtime = getIntFromString(startTime) + duration;
                                        if (endtime > 23)
                                        {
                                            isNextDay = 1;
                                            endtime = endtime - 24;
                                        }
                                        else
                                        {
                                            isNextDay = 0;
                                        }
                                        //printf("Output: end time is %d\n", endtime);
                                        endTimeString = getStringFromInt(endtime);
                                        if (strcmp(words[0], "bookDevice") == 0)
                                        {
                                            needFindRoom = 0;
                                            deviceIndexStart = 5;
                                            strcpy(words[0], "*");
                                        }
                                        else
                                        {
                                            needFindRoom = 1;
                                            deviceIndexStart = 6;
                                            words[0] = words[0] + 3;
                                        }
                                        // format devices string
                                        allDeviceString[0] = 0;
                                        for (h = deviceIndexStart; h < numOfWords; h++)
                                        {
                                            strcat(allDeviceString, words[h]);
                                            strcat(allDeviceString, " ");
                                        }

                                        // finding the room
                                        // search for each room (with suitable room size)
                                        // check if the time slot (from start to end) is occupied by the tanent
                                        h = 0;
                                        int r, q, isOnThatDay;
                                        participant = getIntFromString(words[5]);
                                        if (needFindRoom == 1)
                                        {
                                            // printf("Output: checking rooms (prio) for command %d\n", u);
                                            while (h < SCHEDULE_STRING_LENGTH)
                                            {
                                                memcpy(tempDate, &prioSchedule[h], 10);
                                                tempDate[10] = '\0';
                                                // check if date is the same
                                                //printf("Output: checking date: %s vs %s\n", tempDate, words[2]);
                                                if (strcmp(tempDate, words[2]) == 0)
                                                {
                                                    // find each room and check availability
                                                    //printf("Output: found same date: %s\n", tempDate);
                                                    for (r = 0; r < roomAmount; r++)
                                                    {
                                                        if (roomCapacity[r] >= participant)
                                                        {
                                                            //printf("Output: found room with good size: %s\n", roomNames[r]);
                                                            int tempStart = h + 10 + r * 24 + startTimeInt;
                                                            isOnThatDay = 1;
                                                            char t = i + '0';
                                                            for (q = 0; q < duration; q++)
                                                            {
                                                                if (prioSchedule[tempStart + q] != t)
                                                                {
                                                                    isOnThatDay = 0;
                                                                    break;
                                                                }
                                                            }
                                                            if (isOnThatDay == 1)
                                                            {
                                                                strcpy(room, roomNames[r]);
                                                                break;
                                                            }
                                                        }
                                                    }
                                                }
                                                h = h + oneDayScheduleSize;
                                            }
                                        }
                                        else
                                        {
                                            strcpy(room, "*");
                                        }

                                        if (strcmp(words[1], tenantNames[i]) == 0)
                                        {
                                            if (isNextDay == 0)
                                            {
                                                fprintf(fp, "%-12s%-7s%2s:00                %-13s%-9s%s\n", words[2], words[3], endTimeString, words[0], room, allDeviceString);
                                            }
                                            else
                                            {
                                                fprintf(fp, "%-12s%-7s%2s:00(the next day)  %-13s%-9s%s\n", words[2], words[3], endTimeString, words[0], room, allDeviceString);
                                            }
                                        }
                                        free(words);
                                    }
                                }
                                fprintf(fp, "\n");
                            }
                            fprintf(fp, "\n");

                            // print all rejected command
                            fprintf(fp, "*** Room Booking - REJECTED / Priority ***\n");
                            for (i = 0; i < tenantAmount; i++)
                            {
                                fprintf(fp, "%s has the following bookings:\n", tenantNames[i]);
                                fprintf(fp, "Date        Start  End                  Type         Device           \n");
                                fprintf(fp, "=================================================================\n");
                                for (u = 0; u < priorityRejectedCount; u++)
                                {
                                    char currentCmd[COMMAND_CHAR_SIZE];
                                    strcpy(currentCmd, commandHistory[priorityRejectedCommandIndex[u]]);
                                    numOfWords = getNumOfWord(currentCmd, strlen(currentCmd));
                                    char **words = splitCommand(currentCmd, strlen(currentCmd));
                                    words[1]++;
                                    //printf("Output: comparing %s to %s\n", words[1], tenantNames[i]);
                                    if (strcmp(words[1], tenantNames[i]) == 0)
                                    {
                                        startTime[0] = words[3][0];
                                        startTime[1] = words[3][1];
                                        startTime[2] = 0;
                                        duration = getIntFromString(words[4]);
                                        //printf("Output: duration of [%s] is %d\n", words[4], duration);
                                        startTimeInt = getIntFromString(startTime);
                                        //printf("Output: start time of [%s] is %d\n", startTime, startTimeInt);
                                        endtime = getIntFromString(startTime) + duration;
                                        if (endtime > 23)
                                        {
                                            isNextDay = 1;
                                            endtime = endtime - 24;
                                        }
                                        else
                                        {
                                            isNextDay = 0;
                                        }
                                        //printf("Output: end time is %d\n", endtime);
                                        endTimeString = getStringFromInt(endtime);
                                        if (strcmp(words[0], "bookDevice") == 0)
                                        {
                                            deviceIndexStart = 5;
                                            strcpy(words[0], "*");
                                        }
                                        else
                                        {
                                            deviceIndexStart = 6;
                                            words[0] = words[0] + 3;
                                        }
                                        allDeviceString[0] = 0;
                                        for (h = deviceIndexStart; h < numOfWords; h++)
                                        {
                                            strcat(allDeviceString, words[h]);
                                            strcat(allDeviceString, " ");
                                        }
                                        if (isNextDay == 0)
                                        {
                                            fprintf(fp, "%-12s%-7s%2s:00                %-13s%s\n", words[2], words[3], endTimeString, words[0], allDeviceString);
                                        }
                                        else 
                                        {
                                            fprintf(fp, "%-12s%-7s%2s:00(the next day)  %-13s%s\n", words[2], words[3], endTimeString, words[0], allDeviceString);
                                        }
                                        
                                    }
                                    free(words);
                                }
                                fprintf(fp, "\n");
                            }
                        }

                        // clean up
                        free(endTimeString);
                        fclose(fp);
                        fcfsRejectedCount = 0;
                        priorityRejectedCount = 0;
                        numOfCommand = 0;
                        fcfsSchedule[0] = 0;
                        prioSchedule[0] = 0;
                    }
                    else
                    {
                        strcpy(mode, buf);
                        while (1)
                        {
                            n = read(outputPipe[0][0], buf, COMMAND_CHAR_SIZE);
                            buf[n] = 0;
                            if (strcmp(buf, "&&") != 0)
                            {
                                strcpy(commandHistory[numOfCommand], buf);
                                numOfCommand++;
                            }
                            else
                            {
                                break;
                            }
                        }
                        if (strcmp(mode, "-fcfs") == 0)
                        {
                            n = read(outputPipe[0][0], fcfsSchedule, SCHEDULE_STRING_LENGTH);
                            fcfsSchedule[n] = 0;
                            n = read(outputPipe[0][0], buf, COMMAND_CHAR_SIZE);
                            buf[n] = 0;
                            fcfsRejectedCount = getIntFromString(buf);
                            for (i = 0; i < fcfsRejectedCount; i++)
                            {
                                n = read(outputPipe[0][0], buf, COMMAND_CHAR_SIZE);
                                buf[n] = 0;
                                fcfsRejectedCommandIndex[i] = getIntFromString(buf);
                            }
                        }
                        else if (strcmp(mode, "-prio") == 0)
                        {
                            n = read(outputPipe[0][0], prioSchedule, SCHEDULE_STRING_LENGTH);
                            prioSchedule[n] = 0;
                            n = read(outputPipe[0][0], buf, COMMAND_CHAR_SIZE);
                            buf[n] = 0;
                            priorityRejectedCount = getIntFromString(buf);
                            for (i = 0; i < priorityRejectedCount; i++)
                            {
                                n = read(outputPipe[0][0], buf, COMMAND_CHAR_SIZE);
                                buf[n] = 0;
                                priorityRejectedCommandIndex[i] = getIntFromString(buf);
                            }
                        }
                        else if (strcmp(mode, "-ALL") == 0)
                        {
                            n = read(outputPipe[0][0], fcfsSchedule, SCHEDULE_STRING_LENGTH);
                            fcfsSchedule[n] = 0;
                            n = read(outputPipe[0][0], prioSchedule, SCHEDULE_STRING_LENGTH);
                            prioSchedule[n] = 0;
                            n = read(outputPipe[0][0], buf, COMMAND_CHAR_SIZE);
                            buf[n] = 0;
                            fcfsRejectedCount = getIntFromString(buf);
                            n = read(outputPipe[0][0], buf, COMMAND_CHAR_SIZE);
                            buf[n] = 0;
                            priorityRejectedCount = getIntFromString(buf);
                            for (i = 0; i < fcfsRejectedCount; i++)
                            {
                                n = read(outputPipe[0][0], buf, COMMAND_CHAR_SIZE);
                                buf[n] = 0;
                                fcfsRejectedCommandIndex[i] = getIntFromString(buf);
                            }
                            for (i = 0; i < priorityRejectedCount; i++)
                            {
                                n = read(outputPipe[0][0], buf, COMMAND_CHAR_SIZE);
                                buf[n] = 0;
                                priorityRejectedCommandIndex[i] = getIntFromString(buf);
                            }
                        }
                    }
                }

                // use outputPipe[1][1] to WRITE TO PARENT
                // use outputPipe[0][0] to READ FROM PARENT

                close(outputPipe[1][1]);
                close(outputPipe[0][0]);
                // printf("Output Module: done.\n");
            }
            else
            {
                // Parent
                // close unused pipes
                close(outputPipe[1][1]);
                close(outputPipe[0][0]);
                // STEP 4: fork/pipe Analyzer Module
                int analyzerPipe[2][2];
                pid_t analyzerCid;
                // create pipe, parent use [0][1] write, child use [1][1] write
                for (i = 0; i < 2; i++)
                {
                    if (pipe(analyzerPipe[i]) < 0)
                    {
                        printf("Error: analyzer pipe creation error\n");
                        exit(1);
                    }
                }
                if ((analyzerCid = fork()) < 0)
                {
                    printf("Error: fork analyzer module error\n");
                    exit(1);
                }
                else if (analyzerCid == 0)
                {
                    // TODO: Analyzer module (part 4)
                    close(inputPipe[1][0]);
                    close(inputPipe[0][1]);
                    close(schedulePipe[0][1]);
                    close(schedulePipe[1][0]);
                    close(outputPipe[0][1]);
                    close(outputPipe[1][0]);
                    close(analyzerPipe[1][0]);
                    close(analyzerPipe[0][1]);
                    // printf("Analyzer Module: started.\n");

                    int n;
                    char buf[COMMAND_CHAR_SIZE];

                    // -- Useful Data --
                    char fcfsSchedule[SCHEDULE_STRING_LENGTH + 1];
                    char prioSchedule[SCHEDULE_STRING_LENGTH + 1];
                    int numOfCommand;
                    int fcfsRejectedAmount;
                    int priorityRejectedAmount;
                    int invalidCommandAmount;

                    while (1)
                    {
                        n = read(analyzerPipe[0][0], buf, COMMAND_CHAR_SIZE);
                        buf[n] = 0;
                        // printf("Analyzer module: received [%s]\n", buf);
                        // whole program is finish, end child
                        if (strcmp(buf, "CLOSE") == 0)
                        {
                            // printf("analyzer module: closing...\n");
                            break;
                        }
                        else if (strcmp(buf, "END") == 0)
                        {
                            // printf("Analyzer: received fcfs = %s\n", fcfsSchedule);
                            // printf("Analyzer: received prio = %s\n", prioSchedule);
                            // printf("Analyzer: received numOfCommand = %d\n", numOfCommand);
                            // printf("Analyzer: received fcfsRejecteAmount = %d\n", fcfsRejectedAmount);
                            // printf("Analyzer: received priorityRejecteAmount = %d\n", priorityRejectedAmount);
                            // printf("Analyzer: received invalidCommandAmount = %d\n", invalidCommandAmount);
                            // -- write it here

                            int fcfsAssignedAmount = numOfCommand - fcfsRejectedAmount;
                            int priorityAssignedAmount = numOfCommand - priorityRejectedAmount;

                            int fcfsRoomsUnused[roomAmount];
                            int fcfsWebcamsUnused[webcamAmount];
                            int fcfsMonitorsUnused[monitorAmount];
                            int fcfsProjectorsUnused[projectorAmount];
                            int fcfsScreensUnused[screenAmount];
                            int prioRoomsUnused[roomAmount];
                            int prioWebcamsUnused[webcamAmount];
                            int prioMonitorsUnused[monitorAmount];
                            int prioProjectorsUnused[projectorAmount];
                            int prioScreensUnused[screenAmount];

                            int currentUnused, a, day, hr;
                            int count = 0;
                            for (currentUnused = 0; currentUnused < 5; currentUnused++)
                            {
                                for (a = 0; a < 3; a++)
                                {
                                    for (day = 0; day < 7; day++)
                                    {
                                        for (hr = 0; hr < 24; hr++)
                                        {
                                            if (fcfsSchedule[360 * day + 10 * (day + 1) + 72 * currentUnused + 24 * a + hr] == '_')
                                            {
                                                count++;
                                                switch (currentUnused)
                                                {
                                                case 0:
                                                    fcfsRoomsUnused[a]++;
                                                    break;
                                                case 1:
                                                    fcfsWebcamsUnused[a]++;
                                                    break;
                                                case 2:
                                                    fcfsMonitorsUnused[a]++;
                                                    break;
                                                case 3:
                                                    fcfsProjectorsUnused[a]++;
                                                    break;
                                                case 4:
                                                    fcfsScreensUnused[a]++;
                                                    break;
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                            //printf("%d,", count);
                            for (currentUnused = 0; currentUnused < 5; currentUnused++)
                            {
                                for (a = 0; a < 3; a++)
                                {
                                    for (day = 0; day < 7; day++)
                                    {
                                        for (hr = 0; hr < 24; hr++)
                                        {
                                            if (prioSchedule[360 * day + 10 * (day + 1) + 72 * currentUnused + 24 * a + hr] == '_')
                                            {
                                                switch (currentUnused)
                                                {
                                                case 0:
                                                    prioRoomsUnused[a]++;
                                                    break;
                                                case 1:
                                                    prioWebcamsUnused[a]++;
                                                    break;
                                                case 2:
                                                    prioMonitorsUnused[a]++;
                                                    break;
                                                case 3:
                                                    prioProjectorsUnused[a]++;
                                                    break;
                                                case 4:
                                                    prioScreensUnused[a]++;
                                                    break;
                                                }
                                            }
                                        }
                                    }
                                }
                            }

                            FILE *fp;
                            fp = fopen(DIRECTORY_ANALYSIS_REPORT, "w");
                            fprintf(fp, "*** Room Booking Manager - Summary Report ***\n\n");
                            fprintf(fp, "Performance:\n\n");
                            fprintf(fp, "  For FCFS:\n");
                            fprintf(fp, "            Total Number of Booking Received: %d (%0.1f%%)\n", numOfCommand, (float)numOfCommand / numOfCommand * 100);
                            fprintf(fp, "                  Number of Booking Assigned: %d (%0.1f%%)\n", fcfsAssignedAmount, (float)fcfsAssignedAmount / numOfCommand * 100);
                            fprintf(fp, "                  Number of Booking Rejected: %d (%0.1f%%)\n\n", fcfsRejectedAmount, (float)fcfsRejectedAmount / numOfCommand * 100);
                            fprintf(fp, "            Utilization of Time Slot:\n");

                            int z;

                            for (z = 0; z < 3; z++)
                            {
                                fprintf(fp, "                  %s         - %0.1f%%\n", roomNames[z], (float)(168 - fcfsRoomsUnused[z]) / 168 * 100);
                            }
                            for (z = 0; z < 3; z++)
                            {
                                fprintf(fp, "                  %s     - %0.1f%%\n", webcamNames[z], (float)(168 - fcfsWebcamsUnused[z]) / 168 * 100);
                            }
                            for (z = 0; z < 3; z++)
                            {
                                fprintf(fp, "                  %s     - %0.1f%%\n", monitorNames[z], (float)(168 - fcfsMonitorsUnused[z]) / 168 * 100);
                            }
                            for (z = 0; z < 3; z++)
                            {
                                fprintf(fp, "                  %s   - %0.1f%%\n", projectorNames[z], (float)(168 - fcfsProjectorsUnused[z]) / 168 * 100);
                            }
                            for (z = 0; z < 3; z++)
                            {
                                fprintf(fp, "                  %s     - %0.1f%%\n", screenNames[z], (float)(168 - fcfsScreensUnused[z]) / 168 * 100);
                            }
                            fprintf(fp, "\n            Invalid request(s) made: %d\n", invalidCommandAmount);

                            fprintf(fp, "\n");

                            fprintf(fp, "  For PRIO:\n");
                            fprintf(fp, "            Total Number of Booking Received: %d (%0.1f%%)\n", numOfCommand, (float)numOfCommand / numOfCommand * 100);
                            fprintf(fp, "                  Number of Booking Assigned: %d (%0.1f%%)\n", priorityAssignedAmount, (float)priorityAssignedAmount / numOfCommand * 100);
                            fprintf(fp, "                  Number of Booking Rejected: %d (%0.1f%%)\n\n", priorityRejectedAmount, (float)priorityRejectedAmount / numOfCommand * 100);
                            fprintf(fp, "            Utilization of Time Slot:\n");
                            for (z = 0; z < 3; z++)
                            {
                                fprintf(fp, "                  %s         - %0.1f%%\n", roomNames[z], (float)(168 - prioRoomsUnused[z]) / 168 * 100);
                            }
                            for (z = 0; z < 3; z++)
                            {
                                fprintf(fp, "                  %s     - %0.1f%%\n", webcamNames[z], (float)(168 - prioWebcamsUnused[z]) / 168 * 100);
                            }
                            for (z = 0; z < 3; z++)
                            {
                                fprintf(fp, "                  %s     - %0.1f%%\n", monitorNames[z], (float)(168 - prioMonitorsUnused[z]) / 168 * 100);
                            }
                            for (z = 0; z < 3; z++)
                            {
                                fprintf(fp, "                  %s   - %0.1f%%\n", projectorNames[z], (float)(168 - prioProjectorsUnused[z]) / 168 * 100);
                            }
                            for (z = 0; z < 3; z++)
                            {
                                fprintf(fp, "                  %s     - %0.1f%%\n", screenNames[z], (float)(168 - prioScreensUnused[z]) / 168 * 100);
                            }
                            fprintf(fp, "\n            Invalid request(s) made: %d\n", invalidCommandAmount);

                            fclose(fp);

                            // main objectives:
                            // 1. analyze all schedule from fcfsSchedule and prioSchedule
                            // using to -- USEFUL DATA --
                            // 2. print the fucking report to a file
                        }
                        else if (strcmp(buf, "START") == 0)
                        {
                            n = read(analyzerPipe[0][0], fcfsSchedule, SCHEDULE_STRING_LENGTH);
                            fcfsSchedule[n] = 0;
                            n = read(analyzerPipe[0][0], prioSchedule, SCHEDULE_STRING_LENGTH);
                            prioSchedule[n] = 0;
                            n = read(analyzerPipe[0][0], buf, COMMAND_CHAR_SIZE);
                            buf[n] = 0;
                            numOfCommand = getIntFromString(buf);
                            n = read(analyzerPipe[0][0], buf, COMMAND_CHAR_SIZE);
                            buf[n] = 0;
                            fcfsRejectedAmount = getIntFromString(buf);
                            n = read(analyzerPipe[0][0], buf, COMMAND_CHAR_SIZE);
                            buf[n] = 0;
                            priorityRejectedAmount = getIntFromString(buf);
                            n = read(analyzerPipe[0][0], buf, COMMAND_CHAR_SIZE);
                            buf[n] = 0;
                            invalidCommandAmount = getIntFromString(buf);
                        }
                    }

                    close(analyzerPipe[1][1]);
                    close(analyzerPipe[0][0]);
                    // printf("Analyzer Module: done.\n");
                }
                else
                {
                    // PARENT CODE START HERE
                    close(analyzerPipe[1][1]);
                    close(analyzerPipe[0][0]);
                    // Parent: receive input
                    int n;
                    char commandHistory[MAX_COMMAND][COMMAND_CHAR_SIZE];
                    int numOfCommand = 0;
                    int invalidCount = 0;
                    char buf[COMMAND_CHAR_SIZE];

                    while (1)
                    {
                        write(inputPipe[0][1], "PROMPT", COMMAND_CHAR_SIZE);

                        n = read(inputPipe[1][0], buf, COMMAND_CHAR_SIZE);
                        buf[n] = 0;
                        // printf("Parent: %d char read from pipe: [%s]\n", n, buf);
                        if (strcmp(buf, "endProgram;") == 0)
                        {
                            // printf("Parent: end program command is received.\n");
                            break;
                        }
                        else if (strcmp(buf, "INVALID") == 0)
                        {
                            invalidCount++;
                        }
                        else
                        {
                            char **words = splitCommand(buf, n);
                            int numOfWords = getNumOfWord(buf, n);
                            if (strcmp(words[0], "printBookings") == 0)
                            {
                                // start scheduling module
                                write(schedulePipe[0][1], words[1], COMMAND_CHAR_SIZE);
                                for (i = 0; i < numOfCommand; i++)
                                {
                                    write(schedulePipe[0][1], commandHistory[i], COMMAND_CHAR_SIZE);
                                }
                                write(schedulePipe[0][1], "END", COMMAND_CHAR_SIZE);

                                char bigBuf[SCHEDULE_STRING_LENGTH];
                                int g;
                                // get schedule from schedule module
                                g = read(schedulePipe[1][0], bigBuf, SCHEDULE_STRING_LENGTH);
                                bigBuf[g] = 0;
                                // printf("Parent: %d char read from schedule pipe: [%s]\n", g, bigBuf);
                                // saved to result!
                                if (strcmp(words[1], "-fcfs") == 0)
                                {
                                    strcpy(fcfs, bigBuf);
                                    n = read(schedulePipe[1][0], buf, COMMAND_CHAR_SIZE);
                                    buf[n] = 0;
                                    fcfsRejectedCount = strtol(buf, NULL, 10);
                                    if (fcfsRejectedCount > 0)
                                    {
                                        for (i = 0; i < fcfsRejectedCount; i++)
                                        {
                                            n = read(schedulePipe[1][0], buf, COMMAND_CHAR_SIZE);
                                            buf[n] = 0;
                                            fcfsRejectedCommandIndex[i] = strtol(buf, NULL, 10);
                                        }
                                    }
                                    // printf("Parent: current fcfs = %s\n", fcfs);
                                    // printf("Parent: current fcfsRejectedCount = %d\n", fcfsRejectedCount);
                                    // printf("Parent: current fcfsRejectedCommandIndex = [");
                                    // for (i = 0; i < fcfsRejectedCount; i++)
                                    // {
                                    //     printf("%d, ", fcfsRejectedCommandIndex[i]);
                                    // }
                                    // printf("]\n");
                                }
                                else if (strcmp(words[1], "-prio") == 0)
                                {
                                    strcpy(priority, bigBuf);
                                    n = read(schedulePipe[1][0], buf, COMMAND_CHAR_SIZE);
                                    buf[n] = 0;
                                    priorityRejectedCount = strtol(buf, NULL, 10);
                                    if (priorityRejectedCount > 0)
                                    {
                                        for (i = 0; i < priorityRejectedCount; i++)
                                        {
                                            n = read(schedulePipe[1][0], buf, COMMAND_CHAR_SIZE);
                                            buf[n] = 0;
                                            priorityRejectedCommandIndex[i] = strtol(buf, NULL, 10);
                                        }
                                    }
                                    // printf("Parent: current priority = %s\n", priority);
                                    // printf("Parent: current priorityRejectedCount = %d\n", priorityRejectedCount);
                                    // printf("Parent: current priorityRejectedCommandIndex = [");
                                    // for (i = 0; i < priorityRejectedCount; i++)
                                    // {
                                    //     printf("%d, ", priorityRejectedCommandIndex[i]);
                                    // }
                                    // printf("]\n");
                                }
                                else if (strcmp(words[1], "-ALL") == 0)
                                {
                                    strcpy(fcfs, bigBuf);
                                    g = read(schedulePipe[1][0], bigBuf, SCHEDULE_STRING_LENGTH);
                                    bigBuf[g] = 0;
                                    strcpy(priority, bigBuf);
                                    n = read(schedulePipe[1][0], buf, COMMAND_CHAR_SIZE);
                                    buf[n] = 0;
                                    fcfsRejectedCount = strtol(buf, NULL, 10);
                                    if (fcfsRejectedCount > 0)
                                    {
                                        for (i = 0; i < fcfsRejectedCount; i++)
                                        {
                                            n = read(schedulePipe[1][0], buf, COMMAND_CHAR_SIZE);
                                            buf[n] = 0;
                                            fcfsRejectedCommandIndex[i] = strtol(buf, NULL, 10);
                                        }
                                    }
                                    // printf("Parent: current fcfs = %s\n", fcfs);
                                    // printf("Parent: current fcfsRejectedCount = %d\n", fcfsRejectedCount);
                                    // printf("Parent: current fcfsRejectedCommandIndex = [");
                                    // for (i = 0; i < fcfsRejectedCount; i++)
                                    // {
                                    //     printf("%d, ", fcfsRejectedCommandIndex[i]);
                                    // }
                                    // printf("]\n");
                                    n = read(schedulePipe[1][0], buf, COMMAND_CHAR_SIZE);
                                    buf[n] = 0;
                                    if (strcmp(buf, "&&") == 0)
                                    {
                                        n = read(schedulePipe[1][0], buf, COMMAND_CHAR_SIZE);
                                        buf[n] = 0;
                                        priorityRejectedCount = strtol(buf, NULL, 10);
                                        if (priorityRejectedCount > 0)
                                        {
                                            for (i = 0; i < priorityRejectedCount; i++)
                                            {
                                                n = read(schedulePipe[1][0], buf, COMMAND_CHAR_SIZE);
                                                buf[n] = 0;
                                                priorityRejectedCommandIndex[i] = strtol(buf, NULL, 10);
                                            }
                                        }
                                        // printf("Parent: current priority = %s\n", priority);
                                        // printf("Parent: current priorityRejectedCount = %d\n", priorityRejectedCount);
                                        // printf("Parent: current priorityRejectedCommandIndex = [");
                                        // for (i = 0; i < priorityRejectedCount; i++)
                                        // {
                                        //     printf("%d, ", priorityRejectedCommandIndex[i]);
                                        // }
                                        // printf("]\n");
                                    }
                                }

                                // send to output module
                                char *temp;
                                write(outputPipe[0][1], words[1], COMMAND_CHAR_SIZE);
                                for (i = 0; i < numOfCommand; i++)
                                {
                                    write(outputPipe[0][1], commandHistory[i], COMMAND_CHAR_SIZE);
                                }
                                write(outputPipe[0][1], "&&", COMMAND_CHAR_SIZE);
                                if (strcmp(words[1], "-fcfs") == 0)
                                {
                                    write(outputPipe[0][1], fcfs, SCHEDULE_STRING_LENGTH);
                                    temp = getStringFromInt(fcfsRejectedCount);
                                    write(outputPipe[0][1], temp, COMMAND_CHAR_SIZE);
                                    for (i = 0; i < fcfsRejectedCount; i++)
                                    {
                                        temp = getStringFromInt(fcfsRejectedCommandIndex[i]);
                                        write(outputPipe[0][1], temp, COMMAND_CHAR_SIZE);
                                    }
                                }
                                else if (strcmp(words[1], "-prio") == 0)
                                {
                                    write(outputPipe[0][1], priority, SCHEDULE_STRING_LENGTH);
                                    temp = getStringFromInt(priorityRejectedCount);
                                    write(outputPipe[0][1], temp, COMMAND_CHAR_SIZE);
                                    for (i = 0; i < priorityRejectedCount; i++)
                                    {
                                        temp = getStringFromInt(priorityRejectedCommandIndex[i]);
                                        write(outputPipe[0][1], temp, COMMAND_CHAR_SIZE);
                                    }
                                }
                                else if (strcmp(words[1], "-ALL") == 0)
                                {
                                    write(outputPipe[0][1], fcfs, SCHEDULE_STRING_LENGTH);
                                    write(outputPipe[0][1], priority, SCHEDULE_STRING_LENGTH);
                                    temp = getStringFromInt(fcfsRejectedCount);
                                    write(outputPipe[0][1], temp, COMMAND_CHAR_SIZE);
                                    temp = getStringFromInt(priorityRejectedCount);
                                    write(outputPipe[0][1], temp, COMMAND_CHAR_SIZE);
                                    for (i = 0; i < fcfsRejectedCount; i++)
                                    {
                                        temp = getStringFromInt(fcfsRejectedCommandIndex[i]);
                                        write(outputPipe[0][1], temp, COMMAND_CHAR_SIZE);
                                    }
                                    for (i = 0; i < priorityRejectedCount; i++)
                                    {
                                        temp = getStringFromInt(priorityRejectedCommandIndex[i]);
                                        write(outputPipe[0][1], temp, COMMAND_CHAR_SIZE);
                                    }
                                }
                                free(temp);
                                write(outputPipe[0][1], "END", COMMAND_CHAR_SIZE);

                                // send to analyze module
                                if (strcmp(words[1], "-ALL") == 0)
                                {
                                    char *n = getStringFromInt(numOfCommand);
                                    char *f = getStringFromInt(fcfsRejectedCount);
                                    char *p = getStringFromInt(priorityRejectedCount);
                                    char *i = getStringFromInt(invalidCount);
                                    write(analyzerPipe[0][1], "START", COMMAND_CHAR_SIZE);
                                    write(analyzerPipe[0][1], fcfs, SCHEDULE_STRING_LENGTH);
                                    write(analyzerPipe[0][1], priority, SCHEDULE_STRING_LENGTH);
                                    write(analyzerPipe[0][1], n, COMMAND_CHAR_SIZE);
                                    write(analyzerPipe[0][1], f, COMMAND_CHAR_SIZE);
                                    write(analyzerPipe[0][1], p, COMMAND_CHAR_SIZE);
                                    write(analyzerPipe[0][1], i, COMMAND_CHAR_SIZE);
                                    write(analyzerPipe[0][1], "END", COMMAND_CHAR_SIZE);
                                    free(n);
                                    free(f);
                                    free(p);
                                    free(i);
                                }
                                printf("-> [Done!]\n");
                            }
                            else
                            {
                                // addBooking, addPresentation, addConference, or bookDevice
                                // record the input history
                                strcpy(commandHistory[numOfCommand], buf);
                                //printf("Parent: saved %d:[%s]\n", numOfCommand, commandHistory[numOfCommand]);
                                numOfCommand++;
                                printf("-> [Pending]\n");
                            }
                            free(words);
                        }
                    }

                    // clean up
                    write(schedulePipe[0][1], "CLOSE", COMMAND_CHAR_SIZE);
                    write(outputPipe[0][1], "CLOSE", COMMAND_CHAR_SIZE);
                    write(analyzerPipe[0][1], "CLOSE", COMMAND_CHAR_SIZE);

                    close(inputPipe[1][0]);
                    close(inputPipe[0][1]);
                    close(schedulePipe[1][0]);
                    close(schedulePipe[0][1]);
                    close(outputPipe[1][0]);
                    close(outputPipe[0][1]);
                    close(analyzerPipe[1][0]);
                    close(analyzerPipe[0][1]);

                    pid_t wpid;
                    int status = 0;
                    while ((wpid = wait(&status)) > 0)
                        ;
                    printf("-> Bye!\n");
                }
            }
        }
    }
}