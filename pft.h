#ifndef PFT_H
#define PFT_H

const int SUCCESS=0, FAILURE =-1;

#include <vector>
#include <string>


typedef struct pft_stats_struct{
	int file_num;    //the total number of files processed up to now
	double time_sec; //total time in seconds spent in processing
}pft_stats_struct;


/*
Initialize the pft library.
Argument:
	"n" is the level of parallelism to use (number of parallel ‘file’) commands.
This method should initialize the library with empty statistics.

A failure may happen if a system call fails (e.g. alloc) or if n is not positive.
Return value:
	A valid error message, started with "pft_init error:" should be obtained by using the pft_get_error().
*/
int pft_init(int n);

/*
This function called when the user finished to use the library.
Its purpose is to free the memory/resources that allocated by the library.

The function return error FAILURE if a system call fails.
Return value:
	On success return SUCCESS, on error return FAILURE.
	A valid error message, started with "pft_done error:" should be obtained by using the pft_get_error().
*/
int pft_done();


/*
Set the parallelism level.
Argument:
	"n" is the level of parallelism to use (number of parallel ‘file’) commands.
A failure may happen if a system call fails (e.g. alloc) or n is not positive.
Return value:
	On success return SUCCESS, on error return FAILURE.
	A valid error message, started with "setParallelismLevel error:" should be obtained by using the pft_get_error().
*/
int setParallelismLevel(int n);



// Return the last error message.
// The message should be empty if there was no error since the last initialization.
// This function must not fail.
const std::string pft_get_error();



/*
This method initialize the given pft_stats_struct with the current statistics collected by the pft library.

If statistic is not null, the function assumes that the given structure have been allocated,
and updates file_num to contain the total number of files processed up to now
and time_sec to contain the total time in seconds spent in processing.

A failure may happen if a system call fails or if the statistic is null.

For example, if I call "pft_init", wait 3 seconds, call "pft_find_types" that read 7,000,000 files in 5 seconds, and then call to this
method, the statistics should be: statistic->time_sec = 5 and statistic->file_num = 7,000,000.

Parameter:
	statistic - a pft_stats_struct structure that will be initialized with the current statistics.
Return value:
	On success return SUCCESS, on error return FAILURE.
	A valid error message, started with "pft_get_stats error:" should be obtained by using the pft_get_error().
*/
int pft_get_stats(pft_stats_struct *statistic);


// Clear the statistics setting all to 0.
// The function must not fail.
void pft_clear_stats();



/*
This function uses ‘file’ to calculate the type of each file in the given vector using n parallelism level.
It gets a vector contains the name of the files to check (file_names_vec) and an empty vector (types_vec).
The function runs "file" command on each file in the file_names_vec (even if it is not a valid file) using n parallelism level,
and insert its result to the same index in types_vec.

The function fails if any of his parameters is null, if types_vec is not an empty vector or if a system called failed
(for example fork failed).

Parameters:
	file_names_vec - a vector contains the absolute or relative paths of the files to check.
	types_vec - an empty vector that will be initialized with the results of "file" command on each file in file_names_vec.
Return value:
	On success return SUCCESS, on error return FAILURE.
	A valid error message, started with "pft_find_types error:" should be obtained by using the pft_get_error().
*/
int pft_find_types(std::vector<std::string>& file_names_vec, std::vector<std::string>& types_vec);


#endif /* PFT_H */


