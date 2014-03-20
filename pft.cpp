/*
 * pft.cpp
 *
 *	A program that parallelises severl of linux's 'file' commands
 *	in order to improve its running time.
 *
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

// Function names
static const std::string FUNC_INIT = "pft_init";
static const std::string FUNC_GET_STATS = "pft_get_stats";
static const std::string FUNC_FIND_TYPES = "pft_find_types";
static const std::string FUNC_SET_PARA = "setParallelismLevel";
static const std::string FUNC_DONE = "pft_done";

// Error strings
static const std::string ERROR_STR = " error: ";
static const std::string ERROR_BAD_ALLOC = "Memory allocation error";
static const std::string ERROR_FORK = "Error performing fork";
static const std::string ERROR_CHILD = "Child process error";
static const std::string ERROR_CLOSE = "Error closing a file descriptor";
static const std::string ERROR_PIPE = "Error creating pipe";
static const std::string ERROR_N_PARA = "Invalid parallelism level";
static const std::string ERROR_NULLPTR = "Null pointer exception";
static const std::string ERROR_READ = "Pipe read error";
static const std::string ERROR_WRITE = "Pipe write error";

// Delimiters
static const char NEWLINE = '\n';

// Return values
static const int CODE_SUCCESS = 0;
static const int CODE_FAIL = -1;

// Process chuck size
const int DEFAULT_CHUNK_SIZE = 50;


// Parent <-> Children communication pipes
int** outPipes = nullptr; // Parent writes to children
int** inPipes = nullptr; // Parent reads from children

// Children handling
std::vector<pid_t> children;
static bool childrenAlive = true;

// Stats
int statFileNum;
double statTime;



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

/**
 * Sets the last error to the given error and func name
 * @param func_name the function name
 * @param error the error
 */
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

/**
 * Kills all the child processes.
 */
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

/**
 * Creates all the child processes and pipes
 */
int spawnChildren()
{
	createPipes();
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
				kill(getppid(), SIGUSR1);
			}

			for (int i = 0; i < para_level; ++i)
			{
				if (close(FDReadFromChild(i)) < 0 || close(FDWriteToChild(i)) < 0 )
				{
					kill(getppid(), SIGUSR1);
				}
			}

			int res = execl(FILE_CMD_PATH, FILE_CMD, FILE_FLAG_FLUSH, FILE_FLAG_STDIN, NULL);
			if(res < 0)
			{
				kill(getppid(), SIGUSR1);
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

/**
 * Error handler for child untimely death
 * @param sig the singal number
 */
void childErrorHandler(int sig)
{
	if (sig == SIGUSR1)
	{
		childrenAlive = false;
	}
}

/**
 * Sets the signal handler for the children.
 */
void setSignalHandler()
{
	struct sigaction psa;
	psa.sa_handler = &childErrorHandler;
	psa.sa_flags = SA_NOCLDSTOP;
	sigaction(SIGUSR1, &psa, NULL);
}

/**
 * Initialize the pft library.
 * Argument:
 *	"n" is the level of parallelism to use (number of parallel ‘file’) commands.
 * This method should initialize the library with empty statistics.
 *
 * A failure may happen if a system call fails (e.g. alloc) or if n is not positive.
 * Return value:
 *	A valid error message, started with "pft_init error:" should be obtained by
 *	using the pft_get_error().
 */
int pft_init(int n)
{
	pft_clear_stats();
	setSignalHandler();

	if (setParallelismLevel(n) != CODE_SUCCESS)
	{
		setError(FUNC_INIT, pft_get_error());
		return CODE_FAIL;
	}
	return CODE_SUCCESS;
}



/**
 * This function called when the user finished to use the library.
 * Its purpose is to free the memory/resources that allocated by the library.
 *
 * The function return error FAILURE if a system call fails.
 * Return value:
 *	On success return SUCCESS, on error return FAILURE.
 *	A valid error message, started with "pft_done error:" should be obtained by
 *	using the pft_get_error().
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

/**
 * Set the parallelism level.
 * Argument:
 * 	"n" is the level of parallelism to use (number of parallel ‘file’) commands.
 * A failure may happen if a system call fails (e.g. alloc) or n is not positive.
 * Return value:
 * 	On success return SUCCESS, on error return FAILURE.
 * 	A valid error message, started with "setParallelismLevel error:" should be obtained by
 * 	using the pft_get_error().
 */
int setParallelismLevel(int n)
{
	if(n <= 0)
	{
		setError(FUNC_SET_PARA, ERROR_N_PARA);
		return CODE_FAIL;
	}
	try
	{
		killChildren();
		para_level = n;
		spawnChildren();
	}
	catch (char const* str)
	{
		try
		{
			killChildren();
		}
		catch (char const* str)
		{
			setError(FUNC_SET_PARA, str);
			return CODE_FAIL;
		}
		setError(FUNC_SET_PARA, str);
		return CODE_FAIL;
	}
	return CODE_SUCCESS;
}

/**
 * Return the last error message.
 * The message should be empty if there was no error since the last initialization.
 * This function must not fail.
 */
const std::string pft_get_error()
{
	return last_error;
}

/**
 * This method initialize the given pft_stats_struct with the current statistics collected
 * by the pft library.
 *
 * If statistic is not null, the function assumes that the given structure have been allocated,
 * and updates file_num to contain the total number of files processed up to now
 * and time_sec to contain the total time in seconds spent in processing.
 *
 * A failure may happen if a system call fails or if the statistic is null.
 *
 * For example, if I call "pft_init", wait 3 seconds, call "pft_find_types" that read 7,000,000
 * files in 5 seconds, and then call to this
 * method, the statistics should be: statistic->time_sec = 5 and statistic->file_num = 7,000,000.
 *
 * Parameter:
 * 	statistic - a pft_stats_struct structure that will be initialized with the current statistics.
 * Return value:
 * 	On success return SUCCESS, on error return FAILURE.
 * 	A valid error message, started with "pft_get_stats error:" should be obtained by
 * 	using the pft_get_error().
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

/**
 * Clear the statistics setting all to 0.
 * The function must not fail.
 */
void pft_clear_stats()
{
	statTime = 0;
	statFileNum = 0;
}

/**
 * Returns an fd_set of the reading-from-children file descriptor
 */
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

/**
 * Reads PIPE_BUF bytes from the given child and returns the string.
 */
std::string readAllFromChild(int child)
{
	int fd = FDReadFromChild(child);
	char temp_buff[PIPE_BUF] = "";
	if (read(fd, temp_buff, PIPE_BUF) <= 0)
	{
		throw ERROR_READ;
	}
	std::string str = std::string(temp_buff);
	return str;
}

/**
 * Writes the string str to given child.
 * Returns number of bytes written.
 */
int writeToChild(int child, std::string str)
{
	int write_fd = FDWriteToChild(child);
	int written = write(write_fd, str.c_str(), str.size());
	if (written < 0)
	{
		throw ERROR_WRITE;
	}
	return written;
}

/**
 * Returns the maximal FD in the given set.
 * @return
 */
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

/**
 * This function uses ‘file’ to calculate the type of each file in the given vector
 * using n parallelism level.
 * It gets a vector contains the name of the files to check (file_names_vec) and an
 * empty vector (types_vec).
 * The function runs "file" command on each file in the file_names_vec (even if it is not a valid
 * file) using n parallelism level,
 * and insert its result to the same index in types_vec.
 *
 * The function fails if any of his parameters is null, if types_vec is not an empty vector or
 * if a system called failed
 * (for example fork failed).
 *
 * Parameters:
 * 	file_names_vec - a vector contains the absolute or relative paths of the files to check.
 * 	types_vec - an empty vector that will be initialized with the results of "file" command on
 * 	each file in file_names_vec.
 * Return value:
 * 	On success return SUCCESS, on error return FAILURE.
 * 	A valid error message, started with "pft_find_types error:" should be obtained by
 * 	using the pft_get_error().
 */
int pft_find_types(std::vector<std::string>& file_names_vec, std::vector<std::string>& types_vec)
{
	// Number of files
	int total_files = file_names_vec.size();
	// Init types vector
	types_vec = std::vector<std::string>(total_files, "");
	// index of next file name to write
	int to_write = 0;
	// Number of files to send each time
	int send_files_n = std::min(DEFAULT_CHUNK_SIZE, total_files/para_level);

	// Change parallesim level if it is larger than number of files
	if(total_files < para_level)
	{
		if (setParallelismLevel(total_files) == CODE_FAIL)
		{
			setError(FUNC_FIND_TYPES, pft_get_error());
		}
	}

	// Queue of files for each child
	std::vector< std::queue<int> > positions(para_level);

	// Number of files still not handled
	int remaining_read_files = total_files;

	// FDs
	int max_fd = getMaxFD()+1;
	fd_set reads = getReadFDs();

	// Stats
	timeval begin;
	timeval end;
	if (gettimeofday(&begin, NULL) != CODE_SUCCESS)
	{
		return CODE_FAIL;
	}

	while(remaining_read_files > 0)
	{
		// While not all files handled
		if (!childrenAlive)
		{
			// Child died
			setError(FUNC_FIND_TYPES, ERROR_CHILD);
			return CODE_FAIL;
		}

		// Write to children
		for(int child = 0; child < para_level && to_write < total_files; ++child)
		{
			if(positions[child].empty())
			{
				// Child finished previous work
				std::string filenames = "";
				for (int i = 0; i < send_files_n && to_write < total_files; ++i, ++to_write)
				{
					// Create input string
					filenames += file_names_vec[to_write] + NEWLINE;
					positions[child].push(to_write);
				}
				try
				{
					// Write it to child
					writeToChild(child, filenames);
				}
				catch (const char* str)
				{
					// Write problem
					setError(FUNC_FIND_TYPES, str);
					return CODE_FAIL;
				}
			}
		}

		// Wait until we can read
		fd_set ready_reads = reads;
		select(max_fd, &ready_reads, NULL, NULL, NULL);

		// Read from children
		for(int child = 0; child < para_level; ++child)
		{
			int read_fd = FDReadFromChild(child);
			if(remaining_read_files > 0 && FD_ISSET(read_fd, &ready_reads))
			{
				// Can read from child, and not all files read
				std::string output;
				try
				{
					// Read from child
					output = readAllFromChild(child);
				}
				catch (const char* str)
				{
					// Read problem
					setError(FUNC_FIND_TYPES, str);
					return CODE_FAIL;
				}

				int pos = 0;
				// Add to types vec
				while ((pos = output.find(NEWLINE)) != -1 && remaining_read_files > 0)
				{
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

	// Stats
	if (gettimeofday(&end, NULL) != CODE_SUCCESS)
	{
		return CODE_FAIL;
	}
	statFileNum += total_files;
	statTime += calcTimeDiff(&begin, &end);

	// Done!
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

// a method to print the statistics.
void printStatistic( pft_stats_struct stat){
	printf ("Statistic:\nFiles number=%d\nTime spent=%f\n",stat.file_num,stat.time_sec);
}

int main()
{
	std::vector<std::string> file_names_vec;
	std::vector<std::string> types_vec;

	for(int i=0; i<20000; ++i)
	{
		file_names_vec.push_back("test1.jpg");
		file_names_vec.push_back("test2.txt");
		file_names_vec.push_back("test3.exe");
		file_names_vec.push_back("test4dir");
		file_names_vec.push_back("test5.tar");
	}

	pft_stats_struct stat;

	pft_init(10);

	std::cout << "\nIGNORE THIS RUN:" <<std::endl;
	pft_find_types(file_names_vec, types_vec);
	pft_get_stats(&stat);
	printStatistic(stat);

	std::cout << "\nPARA 7:" << std::endl;
	setParallelismLevel(7);
	pft_clear_stats();
	pft_find_types(file_names_vec, types_vec);
	pft_get_stats(&stat);
	printStatistic(stat);


	std::cout << "\nPARA 6:" << std::endl;
	setParallelismLevel(6);
	pft_clear_stats();
	pft_find_types(file_names_vec, types_vec);
	pft_get_stats(&stat);
	printStatistic(stat);

	std::cout << "\nPARA 5:" << std::endl;
	setParallelismLevel(5);
	pft_clear_stats();
	pft_find_types(file_names_vec, types_vec);
	pft_get_stats(&stat);
	printStatistic(stat);

	std::cout << "\nPARA 4:" << std::endl;
	setParallelismLevel(4);
	pft_clear_stats();
	pft_find_types(file_names_vec, types_vec);
	pft_get_stats(&stat);
	printStatistic(stat);

	std::cout << "\nPARA 3:" << std::endl;
	setParallelismLevel(3);
	pft_clear_stats();
	pft_find_types(file_names_vec, types_vec);
	pft_get_stats(&stat);
	printStatistic(stat);

	std::cout << "\nPARA 2:" << std::endl;
	setParallelismLevel(2);
	pft_clear_stats();
	pft_find_types(file_names_vec, types_vec);
	pft_get_stats(&stat);
	printStatistic(stat);

	std::cout << "\nPARA 1:" << std::endl;
	setParallelismLevel(1);
	pft_clear_stats();
	pft_find_types(file_names_vec, types_vec);
	pft_get_stats(&stat);
	printStatistic(stat);

//	printVector(types_vec);

	pft_done();
}

