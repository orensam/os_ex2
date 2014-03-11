#include "pft.h"
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>
#include <string.h>
#include <sys/time.h>

using namespace std;

// a method to print a vector.
void printVec(vector<string> vec){
	printf ("start to print the vector:\n");
	for (int i=0; i<(int) vec.size(); ++i){
		printf ("%d - %s\n",i, vec[i].c_str());
	}
	printf ("finished to print the vector.\n");
}

// a method to print the statistics.
void printStatistic( pft_stats_struct stat){
	printf ("Statistic:\nFiles number=%d\nTime spent=%f\n",stat.file_num,stat.time_sec);
}

int main( int argc, const char* argv[] ){

	string files[] = {"/bin/ls", "/etc/fstab", "/usr/bin/file"};
	vector<string> in, out;
	pft_stats_struct stat;


	//fill the input vector
	for (int i=0; i<100; ++i){
		in.push_back(files[i%3]);
	}


	printf ("--------------Test starts-----------------\n");
	printf ("\nI make an init with ParallelismLevel=1\n");
	pft_init(1);



	printf ("\nI am printing empty statistics\n");
	//print empty statistics
	pft_get_stats(&stat);
	printStatistic(stat);

	printf ("\nI check the types of file on vector with 100 entries and print the statistic\n");
	//check the types of the input vector and print it..
	pft_find_types (in, out);
	pft_get_stats(&stat);
	printStatistic(stat);

	//clear the previous results.
	pft_clear_stats();
	out.clear();

	printf ("\nI am trying to receive the error message. It should be empty.\n");
	//print error...
	string error=pft_get_error();
	printf("The error message is: %s.\n", error.c_str());


	printf ("\nI change the parallelism level to three and checks the types of the same vector used previously. \n");
	//change the parallelism level and then check the types of the input vector and print it.
	setParallelismLevel(3);
	pft_find_types (in, out);
	printf ("\nI print the statistic, expect better performance (less seconds). \n");
	pft_get_stats(&stat);
	printStatistic(stat);

	printf ("\nI print the types vector received from pft_find_types. \n");
	printVec(out);
	printf ("I call pft_done. \n");
	pft_done();
	printf ("--------------Test ends-----------------\n");


	return 0;
}


