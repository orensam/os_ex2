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
#include <sstream>
#include <iostream>

// Parallelism level
static int para_level;
static bool pipes_inited = false;

// Last error in the library
static std::string last_error = "";

// file program command
static const char* FILE_CMD_PATH = "/usr/bin/file";
static const char* FILE_CMD = "file";
static const char* FILE_FLAG_FLUSH = "-n";
static const char* FILE_FLAG_STDIN = "-f-";

// Process chuck size
const int CHUNK_SIZE = 50;

// Children pids array
//std::vector<pid_t> children;

// Parent <-> Children communication pipes
int** outPipes; // Parent writes to children
int** inPipes; // Parent reads from children

// Stats
int total_time;
int total_file_num;


int FDReadFromParent(int child_num)
{
	return outPipes[child_num][0];
}

int FDWriteToChild(int child_num)
{
	return outPipes[child_num][1];
}

int FDReadFromChild(int child_num)
{
	return inPipes[child_num][0];
}

int FDWriteToParent(int child_num)
{
	return inPipes[child_num][1];
}

/**
 * Creates para_level pipes for reading and para_level pipes for writing.
 * Saved into inPipes and outPipes respectively
 */
int createPipes()
{
	inPipes = new int*[para_level];
	outPipes = new int*[para_level];

	for (int child = 0; child < para_level; ++child)
	{
		// HANDLE ERRORS!
		inPipes[child] = new int[2];
		outPipes[child] = new int[2];
		pipe(inPipes[child]);
		pipe(outPipes[child]);
	}
	pipes_inited = true;
	return 0;
}



int spawnChildren()
{
	createPipes();
	pid_t parent_pid = getpid();
	pid_t pid;

	for(int child = 0; child < para_level; ++child)
	{
		pid_t pid = fork();

		if(pid < 0)
		{
			//ERROR
		}

		else if (pid == 0)
		{
			std::cout << "I am a spawned child! my pid is:" << getpid() << std::endl;
			dup2(FDReadFromParent(child), STDIN_FILENO);
			dup2(FDWriteToParent(child), STDOUT_FILENO);

			close(FDReadFromChild(child));
			close(FDWriteToParent(child));
			close(FDReadFromParent(child));
			close(FDWriteToChild(child));

			int res = execl(FILE_CMD_PATH, FILE_CMD, FILE_FLAG_FLUSH, FILE_FLAG_STDIN, NULL);
			std::cout << "After execl, result: " << res << std::endl;
			// ERROR
		}
		else
		{
			close(FDWriteToParent(child));
			close(FDReadFromParent(child));
			//ERROR
		}
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
	std::cout << "Start init" << std::endl;
	pft_clear_stats();
	setParallelismLevel(n);
	std::cout << "End init" << std::endl;
}


int killChildren()
{
	if (pipes_inited)
	{
		for(int child = 0; child < para_level; ++child)
		{
			close(FDReadFromChild(child));
			close(FDWriteToChild(child));
			delete[] inPipes[child];
			delete[] outPipes[child];
		}
		delete[] inPipes;
		delete[] outPipes;
	}
	pipes_inited = false;
	return 0;
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
	return killChildren();
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
	killChildren();
	para_level = n;
	spawnChildren();
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
	statistic->time_sec = total_time;
	statistic->file_num = total_file_num;
	return 0;
}

/*
 * Clear the statistics setting all to 0.
 * The function must not fail.
 */
void pft_clear_stats()
{
	total_time = 0;
	total_file_num = 0;
}



std::pair<fd_set, fd_set> getReadWriteFDs()
{
	fd_set reads;
	fd_set writes;
	FD_ZERO(&reads);
	FD_ZERO(&writes);
	for(int child = 0; child < para_level; child++)
	{
		// Add parent read+write ends to sets
		FD_SET(FDReadFromChild(child), &reads);
		FD_SET(FDWriteToChild(child), &writes);
	}
	std::pair<fd_set, fd_set> fds = std::make_pair(reads, writes);
	return fds;
}

std::string readAllFromFD(int fd)
{
	std::string str = "";
	char temp_buff[PIPE_BUF] = "";
	while (read(fd, temp_buff, PIPE_BUF) > 0)
	{
		str += temp_buff;
	}
	return str;
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
	std::cout << "Starting findTypes" << std::endl;
	int total_files = file_names_vec.size();
	types_vec = std::vector<std::string>(para_level);

	if(total_files < para_level)
	{
		setParallelismLevel(total_files);
	}

	int remaining_files = total_files;
	int last_sent = 0;
	std::vector< std::pair<int, int> > positions(para_level, std::make_pair(0,0));

	std::pair<fd_set, fd_set> fds = getReadWriteFDs();
	fd_set reads = fds.first;
	fd_set writes = fds.second;
	for(int child = 0; child < para_level; ++child)
	{
		int read_fd = FDReadFromChild(child);
		int write_fd = FDWriteToChild(child);
		if(FD_ISSET(read_fd, &reads))
		{
			std::cout << "Read FD: " << read_fd << std::endl;
		}

		if(FD_ISSET(write_fd, &writes))
		{
			std::cout << "Write FD: " << write_fd << std::endl;
		}
	}

	while(remaining_files != 0)
	{
		std::cout << "In while. Remaining files: " << remaining_files << std::endl;
		fd_set ready_reads = reads;
		fd_set ready_writes =  writes;


		int n_ready_writes = para_level;

		// Writing loop
		for(int child = 0; child < para_level; ++child)
		{

			std::cout << "Handling writing to child " << child << std::endl;

			int write_fd = FDWriteToChild(child);
			if(FD_ISSET(write_fd, &ready_writes))
			{
				//send min(CHUNK_ZISE,remainig_files/ready) files
				int send_files_n = std::min(CHUNK_SIZE, remaining_files/n_ready_writes);
				positions[child].first = last_sent;
				int j;
				for(j = positions[child].first; j < positions[child].first + send_files_n; j++)
				{
					int written = write(write_fd, file_names_vec[j].c_str(), file_names_vec[j].size());
					if (!written)
					{
						//ERROR
					}
				}
				last_sent = j;
			}
		}

		std::cout << "Before read SELECT"  << std::endl;
		select(para_level, &ready_reads, NULL, NULL, NULL); //add timeout?
		std::cout << "After read SELECT"  << std::endl;

		for(int child = 0; child < para_level; ++child)
		{
			std::cout << "Handling read from child " << child << std::endl;
			int read_fd = FDReadFromChild(child);
			if(FD_ISSET(read_fd, &ready_reads))
			{
				std::istringstream output(readAllFromFD(read_fd));
				std::string line;
				int startPos = positions[child].first;
				while (std::getline(output, line))
				{
					types_vec.insert(types_vec.begin()+startPos, line);
					startPos++;
					remaining_files--;
				}
			}
		}

//		std::cout << "before write select" << std::endl;
//		n_ready_writes = select(para_level, NULL, &ready_writes, NULL, NULL); //add timeout?
//		std::cout << "After write select. n_ready_writes: " << n_ready_writes << std::endl;
	}
	std::cout << "After first SELECT"  << std::endl;

	return 0;
}

void printVector(std::vector<std::string>& vec)
{
	for (unsigned int i=0; i<vec.size(); ++i)
	{
		std::cout << vec[i] << std::endl;
	}
}
int main()
{
	pft_init(3);
	sleep(1);
	std::vector<std::string> file_names_vec;
	std::vector<std::string> types_vec;
	file_names_vec.push_back("test1.pdf");
	file_names_vec.push_back("test2.tar");
	file_names_vec.push_back("test3.zip");
	pft_find_types(file_names_vec, types_vec);
	printVector(types_vec);
}






