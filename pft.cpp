/*
 * pft.cpp
 *
 *  Created on: Mar 10, 2014
 *      Author: orensam
 */

#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <string>
#include <algorithm>
#include <unistd.h>
#include <limits.h>
#include <iostream>
#include <queue>
#include <exception>

#include "pft.h"

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

static const std::string ERROR_STR = " error: ";
static const std::string FUNC_INIT = "pft_init";
static const std::string FUNC_GET_STATS = "pft_get_stats";
static const std::string FUNC_FIND_TYPES = "pft_find_types";
static const std::string FUNC_SET_PARA = "setParallelismLevel";
static const std::string FUNC_DONE = "pft_done";

static const std::string ERROR_BAD_ALLOC = "Memory allocation error";
static const std::string ERROR_FORK = "Fork error";
static const std::string ERROR_DUP = "Dup error";
static const std::string ERROR_CLOSE = "Close error";
static const std::string ERROR_PIPE = "Pipe error";
static const std::string ERROR_EXECL = "Execl error";
static const std::string ERROR_N_PARA = "Invalid parallelism level";
static const std::string ERROR_NULLPTR = "Null pointer exception";
static const std::string ERROR_READ = "Pipe read error";
static const std::string ERROR_WRITE = "Pipe write error";

static const char NEWLINE = '\n';


// Return values
static const int CODE_SUCCESS = 0;
static const int CODE_FAIL = -1;

// Process chuck size
const int DEFAULT_CHUNK_SIZE = 50;

// Parent <-> Children communication pipes
//std::vector<int*> outPipes; // Parent writes to children
//std::vector<int*> inPipes; // Parent reads from children
int** outPipes = nullptr; // Parent writes to children
int** inPipes = nullptr; // Parent reads from children

std::vector<pid_t> children;

// Stats
double statTime;
int statFileNum;



/**
 * Returns the file descriptor for child #childNum to read from parent
 */
int FDReadFromParent(int childNum)
{
	return outPipes[childNum][0];
}

/**
 * Returns the file descriptor for parent to write to child #childNum
 */
int FDWriteToChild(int childNum)
{
	return outPipes[childNum][1];
}

/**
 * Returns the file descriptor for parent to read from child #childNum
 */
int FDReadFromChild(int childNum)
{
	return inPipes[childNum][0];
}

/**
 * Returns the file descriptor for child #childNum to write to parent
 */
int FDWriteToParent(int childNum)
{
	return inPipes[childNum][1];
}

void setError(const std::string& func_name, const std::string& error)
{
	last_error = func_name + ERROR_STR + error;
}

/**
 * Creates para_level pipes for reading and para_level pipes for writing.
 * Saved into inPipes and outPipes respectively.
 */
int createPipes()
{
	inPipes = new int*[para_level];
	outPipes = new int*[para_level];

	if (!inPipes || !outPipes)
	{
		throw ERROR_BAD_ALLOC;
	}

	for (int child = 0; child < para_level; ++child)
	{
		inPipes[child] = nullptr;
		outPipes[child] = nullptr;
	}

	for (int child = 0; child < para_level; ++child)
	{
		// alloc error
		inPipes[child] = new int[2];
		outPipes[child] = new int[2];

		if (!inPipes[child] || !outPipes[child])
		{
			throw ERROR_BAD_ALLOC;
		}

		// pipe error
		if ( pipe(inPipes[child]) < 0 || pipe(outPipes[child]) < 0)
		{
			throw ERROR_PIPE;
		}
	}
	pipes_inited = true;
	return CODE_SUCCESS;
}

int killChildren()
{
	if (pipes_inited)
	{
		for(int child = 0; child < para_level; ++child)
		{
			if (close(FDReadFromChild(child)) < 0 || close(FDWriteToChild(child)) < 0)
			{
				throw ERROR_CLOSE;
			}
			delete[] inPipes[child];
			delete[] outPipes[child];
			waitpid(children[child], NULL, 0);
		}
		delete[] inPipes;
		delete[] outPipes;
	}
	pipes_inited = false;
	return CODE_SUCCESS;
}

int spawnChildren()
{
	try
	{
		createPipes();
	}
	catch (char const* str)
	{
		killChildren();
		throw;
	}

	for(int child = 0; child < para_level; ++child)
	{
		pid_t pid = fork();

		if(pid < 0)
		{
			throw ERROR_FORK;
		}

		else if (pid == 0)
		{
			if(dup2(FDReadFromParent(child), STDIN_FILENO) < 0 ||
			   dup2(FDWriteToParent(child), STDOUT_FILENO) < 0)
			{
				throw ERROR_DUP;
			}

			for (int i = 0; i < para_level; ++i)
			{
				if (close(FDReadFromChild(i)) < 0 || close(FDWriteToChild(i)) < 0 )
				{
					throw ERROR_CLOSE;
				}
			}

			int res = execl(FILE_CMD_PATH, FILE_CMD, FILE_FLAG_FLUSH, FILE_FLAG_STDIN, NULL);
			if(res < 0)
			{
				throw ERROR_EXECL;
			}

		}
		else
		{
			children.push_back(pid);
			if(close(FDWriteToParent(child)) < 0 || close(FDReadFromParent(child)) < 0)
			{
				throw ERROR_CLOSE;
			}
		}
	}
	return CODE_SUCCESS;
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
	pft_clear_stats();
	try
	{
		setParallelismLevel(n);
	}
	catch (char const* str)
	{
		setError(FUNC_INIT, str);
		return CODE_FAIL;
	}
	return CODE_SUCCESS;
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
	try

	{
		return killChildren();
	}
	catch (const char* str)
	{
		setError(FUNC_DONE, str);
		return CODE_FAIL;
	}
	return CODE_SUCCESS;
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
		setError(FUNC_SET_PARA, ERROR_N_PARA);
		throw ERROR_N_PARA;
	}
	try
	{
		killChildren();
		para_level = n;
		spawnChildren();
	}
	catch (char const* str)
	{
		setError(FUNC_SET_PARA, str);
	}
	return CODE_SUCCESS;
}

/*
// Return the last error message.
// The message should be empty if there was no error since the last initialization.
// This function must not fail.
 */

const std::string pft_get_error()
{
	return last_error;
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
		setError(FUNC_GET_STATS, ERROR_NULLPTR);
		return CODE_FAIL;
	}
	statistic->time_sec = statTime;
	statistic->file_num = statFileNum;
	return CODE_SUCCESS;
}

/*
 * Clear the statistics setting all to 0.
 * The function must not fail.
 */
void pft_clear_stats()
{
	statTime = 0;
	statFileNum = 0;
}



fd_set getReadFDs()
{
	fd_set reads;
	FD_ZERO(&reads);
	for(int child = 0; child < para_level; child++)
	{
		// Add parent read+write ends to sets
		FD_SET(FDReadFromChild(child), &reads);
	}
	return reads;
}

std::string readAllFromFD(int fd)
{
	char temp_buff[PIPE_BUF] = "";
	if (read(fd, temp_buff, PIPE_BUF) < 0)
	{
		throw ERROR_READ;
	}
	std::string str = std::string(temp_buff);
	return str;
}

int getMaxFD()
{
	int max_fd = 0;
	for(int child = 0; child < para_level; child++)
	{
		int child_fd = FDReadFromChild(child);
		if (child_fd > max_fd)
		{
			max_fd = child_fd;
		}
	}
	return max_fd;
}


/**
 * Calculates the time difference between two given timevals.
 */
double calcTimeDiff(timeval* t1, timeval* t2)
{
	timeval res;
	timersub(t2, t1, &res);
	return res.tv_sec + res.tv_usec / 1000000.0;
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
	types_vec = std::vector<std::string>(total_files, "");
	int to_write = 0;
	int send_files_n = std::min(DEFAULT_CHUNK_SIZE, total_files/para_level);

	if(total_files < para_level)
	{
		setParallelismLevel(total_files);
	}

	std::vector< std::queue<int> > positions(para_level);
	int remaining_read_files = total_files;
	int max_fd = getMaxFD()+1;
	fd_set reads = getReadFDs();

	timeval begin;
	timeval end;

	if (gettimeofday(&begin, NULL) != CODE_SUCCESS)
	{
		return CODE_FAIL;
	}
	while(remaining_read_files > 0)
	{
		// Write to children
		for(int child = 0; child < para_level && to_write < total_files; ++child)
		{
			if(positions[child].empty())
			{
//				std::cout << "CAN WRITE (empty queue) to child " << child << ". to_write: " << to_write << std::endl;
				std::string filenames = "";
				for (int i = 0; i < send_files_n && to_write < total_files; ++i, ++to_write)
				{
					filenames += file_names_vec[to_write] + NEWLINE;
					positions[child].push(to_write);
//					std::cout << "child: " << child << " index: " << to_write << std::endl;
				}

				int write_fd = FDWriteToChild(child);
//				std::cout << "Writing the string: '" << filenames << "' to child " << child << std::endl;
				int written = write(write_fd, filenames.c_str(), filenames.size());
//				std::cout << "Wrote " << written << " bytes to child " << child << std::endl;
				if (written < 0)
				{
					setError(FUNC_FIND_TYPES, ERROR_WRITE);
					return CODE_FAIL;
				}
			}
		}


		fd_set ready_reads(reads);
		select(max_fd, &ready_reads, NULL, NULL, NULL);

		// Read from children
		for(int child = 0; child < para_level; ++child)
		{
//			std::cout << "Handling read from child " << child << std::endl;
			int read_fd = FDReadFromChild(child);
			if(remaining_read_files > 0 && FD_ISSET(read_fd, &ready_reads))
			{
				std::string output;
				try
				{
					output = readAllFromFD(read_fd);
				}
				catch (const char* str)
				{
					setError(FUNC_FIND_TYPES, str);
					return CODE_FAIL;
				}

//				std::cout << "Read from child " << child << " output: "<< output << std::endl;
				int pos = 0;
				while ((pos = output.find(NEWLINE)) != -1 && remaining_read_files > 0)
				{
//					std::cout << "read child: " << child << ", index of newline: " << pos << ", file_vec index: " << positions[child].front() << " Got string: " << output.substr(0, pos) << std::endl;
					types_vec[positions[child].front()].append(output.substr(0, pos));
					positions[child].pop();
					output = output.substr(pos + 1);
					remaining_read_files--;
				}
				if (!positions[child].empty())
				{
					types_vec[positions[child].front()] += output;
				}
			}
		}
	}

	if (gettimeofday(&end, NULL) != CODE_SUCCESS)
	{
		return CODE_FAIL;
	}

	statFileNum = total_files;
	statTime = calcTimeDiff(&begin, &end);
	return CODE_SUCCESS;
}

void printVector(std::vector<std::string>& vec)
{
	std::cout << "\n\n OUTPUT VECTOR:" << std::endl;
	for (unsigned int i=0; i<vec.size(); ++i)
	{
		std::cout << vec[i] << std::endl;
	}
}

int main()
{
	pft_init(200);
	sleep(1);
	std::vector<std::string> file_names_vec;
	std::vector<std::string> types_vec;

	for(int i=0; i<50000; ++i)
	{
		file_names_vec.push_back("test1.pdf");
		file_names_vec.push_back("test2.tar");
		file_names_vec.push_back("test3.zip");
	}

//	file_names_vec.push_back("file1");
//	file_names_vec.push_back("file2");
//	file_names_vec.push_back("file3");
//	file_names_vec.push_back("file4");
//	file_names_vec.push_back("file5");
//	file_names_vec.push_back("file6");
//	file_names_vec.push_back("file7");
//	file_names_vec.push_back("file8");
//	file_names_vec.push_back("file9");
//	file_names_vec.push_back("file1");
//	file_names_vec.push_back("file2");
//	file_names_vec.push_back("file3");
//	file_names_vec.push_back("file4");
//	file_names_vec.push_back("file5");
//	file_names_vec.push_back("file6");
//	file_names_vec.push_back("file7");
//	file_names_vec.push_back("file8");
//	file_names_vec.push_back("file9");

	pft_find_types(file_names_vec, types_vec);
	printVector(types_vec);
	pft_done();
}






