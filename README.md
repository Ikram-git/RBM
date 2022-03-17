# RBM
#This a group project of Operating system course. I have worked in a group of 3 students to develop a Room Booking manager

A Room booking system was developed 10 years ago by a company, which has 3 meeting rooms available for booking along with other add on’s like webcam, projectors, monitors e.t.c. All three rooms have different capacities. The problem with the current booking system is that if the item user wants to book is already reserved, the system simply rejects the user’s request.

This project aims to improve on the above scenario. In this project different scheduling mechanism will be used to prioritize user request so that maximum requests are satisfied thus increasing the utilization of rooms. All scheduling algorithms will generate different results, thus after generating these results, we will compare them and use the algorithm that maximizes the utilization of system, therefore increasing company’s revenue.

Scope

The project aims at covering various topics covered in Operating Systems:

•	Scheduling Algorithms- This is most important OS concept used in this project. In this project we have used two such algorithms i-e First Come First Serve and Priority Scheduling algorithms.
•	 Another important phenomenon of Operating systems executed in this project is creation and communication of parent and child processes by using system call fork(). The process that invokes fork is parent process and newly created process is child process
•	Parents and child have to communicate with each other, this communication is achieved by using pipe. By using pipe read and write operations are performed between parent and child processes.


Concept

The project uses Scheduling algorithms to Improve the effectiveness and utilization of RBM (Room Booking Manager.

There are two scheduling algorithms used in this project:

•	First Come First Serve (FCFS) :In this algorithm request’s of booking are satisfied in the order of arrival.

•	Priority Scheduling: This algorithm is more complex than FCFS as priority is assigned to booking request’s. Priority is assigned keeping in account the utilization and effectiveness of RBM.

Program Structure

At the start of our program we have defined variable i-e  arrays containing tenants, monitor screens projectors,  webcams and number of numbers are defined. Arrays for use in scheduling  algorithms are also defined at start.
To make our validation checks easier later in the program, certain utility functions are defined. Function to check words in a letter string, function to covert integer to string and vice versa.

Then main function is defined. It contains the input module. We have used parent and child processes who do communication via a pipe. In input module, all the inputs from users is taken i-e all details of meeting are taken from users. Then in the main  function we have applied error handling checks to make sure the formats of each individual items/inputs is correct. For example one validation check could be that we have to make sure that the names of devices (i-e projectors,monitors, webcams and screens) entered by users are correct.
Scheduling algorithms are used to make schedules i-e by using number of commands and the command history. The operations are described below:
1)  Schedules are made using both FCFS and priority scheduling
2) Check the mode of operation i-e if mode is FCFS then use FCFS schedule and if mode is priority then use priority scheduling algorithm.

Scheduling Module:

Then programs are written to implement FCFS and priority scheduling. As programs implement the algorithms by using the availability of devices. As individual functions are written to check each device/object’s availability and if so add them to the schedule. Separate functions are written to store rejected request’s by both schedules and thus those request’s  that can’t be accepted are added into respective functions

Furthermore; main function has the output module as well. After taking the input from users, the booking history has to be stored in a file, thus all the bookings are written into a file
Following operations are done on file in the program that follows:

1)  The bookings that are accepted by FCFS algorithm are printed in file: Certain    error checks are applied to make sure that bookings we display here are accepted. It is made sure the that format of booking is in the following order - Date Start End Type Room Device 
Programs to search room of suitable size for each request in a particular time slot are written. Moreover, we have to make sure the number of participants are also appropriate according to room capacities. Then we print the individual meeting information i-e name of tenant, time slot, room, and devices booked

2)   The bookings that are rejected by FCFS algorithm are printed in file: In this segment of program, it has been made sure that the bookings rejected by FCFS are printed in correct format. As a loop is run and all rejected bookings are printed with all the information (i-e devices, room, date, start and end time).

3)  The bookings that are accepted by Priority scheduling are printed in file:
 In this segment, the room with appropriate capacity,availability of devices in room, availability of room on the requested date is found. If the above conditions satisfy by priority Scheduling, all of the accepted bookings are printed one by one in the following format: Date Start End Type Room Device.

4)  The bookings that are rejected by Priority algorithm are printed in file: By comparing the arrays that store user input and the array storing our priority scheduling, we filter those requests have to be rejected due to unavailability or inappropriate demand. The details of such are printed one by one.

Analyzer Module: In this part of program the results of both scheduling algorithms are compared and the algorithm that maximizes utilization is configured. Functions are written to count number of unused devices in case of each scheduling algorithms. The results of analyzer module are then sent output module.

