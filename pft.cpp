/*
 * pft.cpp
 *
 *  Created on: Mar 10, 2014
 *      Author: orensam
 */

#include <sys/time.h>
#include <string>
#include "pft.h"
#include <algorithm>
#include <unistd.h>
#include <limits.h>

// Parallelism level
static int para_level;

// Last error in the library
static std::string last_error = "";

// file program command
static const char* FILE_CMD_PATH = "/bin/file";
static const char* FILE_CMD = "file";
static const char* FILE_FLAGS = "-n -f-";

// Process chuck size
const int CHUNK_SIZE = 50;

// Children pids array
//std::vector<pid_t> children;

// Parent <-> Children communication pipes
int** outPipes; // Parent writes to children
int** inPipes; // Parent reads from children




/**
 * Creates para_level pipes for reading and para_level pipes for writing.
 * Saved into inPipes and outPipes respectively
 */
int createPipes()
{

	inPipes = new int*[para_level];
	outPipes = new int*[para_level];

	for (int i = 0; i < para_level; ++i) {
		inPipes[i] = new int[2];
		outPipes[i] = new int[2];
	}
	std::for_each(inPipes, inPipes + sizeof(inPipes), pipe);
	std::for_each(outPipes, outPipes + sizeof(outPipes), pipe);

	// Create fd_sets from the in/out pipes.
	fd_set reads;
	fd_set writes;
	FD_ZERO(&reads);
	FD_ZERO(&writes);
	for(int i = 0; i < para_level; i++)
	{
		FD_SET(inPipes[i][0], &reads);
		FD_SET(outPipes[i][1], &writes);
	}
}


/*
Initialize the pft library.
Argument:
	"n" is the level of parallelism to use (number of parallel ‘file’) commands.
This method should initialize the library with empty statistics.

A failure may happen if a system call fails (e.g. alloc) or if n is not positive.
Return value:
	A valid error message, started with "pft_init error:" should be obtained by using the pft_get_error().
 */
int pft_init(int n)
{
	setParallelismLevel(n);
	createPipes();

//	try
//	{
//		children = std::vector<pid_t>(n);
//	}
//	catch (std::bad_alloc &e)
//	{
//		// ERROR
//	}
}


int killPipes()
{
	for(int i =0; i < para_level; i++)
	{
		close(inPipes[i][0]);
		close(outPipes[i][0]);
		close(inPipes[i][1]);
		close(outPipes[i][1]);
		//send sig to child i to commit suicide
	}
}



/*
This function called when the user finished to use the library.
Its purpose is to free the memory/resources that allocated by the library.

The function return error FAILURE if a system call fails.
Return value:
	On success return SUCCESS, on error return FAILURE.
	A valid error message, started with "pft_done error:" should be obtained by using the pft_get_error().
 */

int pft_done()
{
	return killPipes();
}


/*
Set the parallelism level.
Argument:
	"n" is the level of parallelism to use (number of parallel ‘file’) commands.
A failure may happen if a system call fails (e.g. alloc) or n is not positive.
Return value:
	On success return SUCCESS, on error return FAILURE.
	A valid error message, started with "setParallelismLevel error:" should be obtained by using the pft_get_error().
 */

int setParallelismLevel(int n)
{
	if(n <= 0)
	{
		//ERROR
	}
	para_level = n;
	killPipes();
	createPipes();
}

/*
// Return the last error message.
// The message should be empty if there was no error since the last initialization.
// This function must not fail.
 */

const std::string pft_get_error()
{
}

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

int pft_get_stats(pft_stats_struct* statistic)
{
	if (!statistic)
	{
		// ERROR
	}

}

/*
 * Clear the statistics setting all to 0.
 * The function must not fail.
 */
void pft_clear_stats()
{
}

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
int pft_find_types(std::vector<std::string>& file_names_vec, std::vector<std::string>& types_vec)
{
	int total_files = file_names_vec.size();

	if(total_files < para_level)
	{
		setParallelismLevel(total_files);
	}

	int remaining_files = total_files;

	int last_sent = -1;
	pid_t parent_pid = getpid();
	pid_t pid;

	for(int i = 0; i < para_level; ++i)
	{
		pid = fork();
		if(pid < 0)
		{
			//ERROR
		}
		if(pid == 0)
		{
			// In child process
			dup2(outPipes[i][0], STDIN_FILENO);
			dup2(inPipes[i][1], STDOUT_FILENO);

			close(inPipes[i][0]);
			close(outPipes[i][1]);
			close(inPipes[i][1]);
			close(outPipes[i][0]);

			execl(FILE_CMD_PATH, FILE_CMD, FILE_FLAGS, NULL);

			break;

		}
		else
		{
			//In parent
			//children[i] = pid;
		}
	}

	if (pid == parent_pid)
	{
		// Parent work
		fd_set reads;
		fd_set writes;
		FD_ZERO(&reads);
		FD_ZERO(&writes);
		for(int i = 0; i < para_level; i++)
		{
			FD_SET(inPipes[i][0], &reads);
			FD_SET(outPipes[i][1], &writes);
		}
		while(remaining_files != 0)
		{
			fd_set temp_reads = reads;
			fd_set temp_writes =  writes;
			select(para_level, &temp_reads, NULL, NULL, NULL);//add time
			int ready = select(para_level, NULL, &temp_writes, NULL, NULL);//add time

			for(int i = 0; i<para_level; i++)
			{
				if(FD_ISSET(inPipes[i][0], &temp_reads))
				{
					char temp_buff[PIPE_BUF];
					read(inPipes[i][0], temp_buff, PIPE_BUF);
					while()
						if(types_vec.end().find("\n") == -1)
						{
							types_vec.end() += getline(temp_buff);
						}
						else
						{
						types_vec.push_back(std::string(temp_buff));
						//remaining_files--;
						}
				}
				if(FD_ISSET(inPipes[i][1], &temp_writes))
				{
					//send min(CHUNK_ZISE,remainig_files/ready) files
					int send_files_n = std::min(CHUNK_SIZE,remaining_files/ready);
					int j;
					for(j = last_sent; j < last_sent + send_files_n; j++)
					{
						write(outPipes[i][1], file_names_vec[j].c_str(), file_names_vec[j].size());

					}
					last_sent = j;
				}
			}
		}
	}

}




