/* By Kirill Aristarkhov and Brandon Katz for CS140 S22 
 * File:      flood.cpp
 * Purpose:   Flood fill algorithm for simultanious work on separate regions of an image.
 * Compile:   (on CSIL) g++ -Wall -std=c++11 -o flood flood.cpp -fopenmp -lpthread
 *            (on MacOS) g++ -Wall -std=c++11 -o flood flood.cpp -Xpreprocessor -fopenmp -lomp -lpthread
 * Run:       ./flood <mode: grid or load>
 * Input:     A file with image parameters and desired number of points to fill.
 * Output:    A file with the image filled with random colors in desired regions.
 */
#include <iostream>
#include <fstream>
#include <omp.h>
#include <pthread.h>
#include <string>
#include <queue>
#include <time.h>
#include <sys/time.h>

using namespace std;

void Usage(int argnum);
int*** Create_blank_matrix(int thread_count);
int*** Create_contour_matrix(int thread_count, ifstream &input);
void Delete_matrix();
void Matrix_to_file(ofstream &outfile);
void* Generate_line(void* line_gen_args);
bool isValid(int row, int col, int red, int green, int blue);
bool shouldIQuit(int row, int col, int red, int green, int blue);
void* Spreading_circles(void* circle_gen_args);
void Get_args(int &size, int &lines, int &thread_count);
void Generate_grid(int lines, int thread_count);
void Get_params(int &point_count, queue<pair<int,int>> *starting_points);

struct line_gen_params{
	int*** matrix;
	int rows;
	int cols;
	int my_id;
	int workload;
};

struct circle_params{
	int x;
	int y;
};

int rows = 0;
int cols = 0;
int*** matrix = NULL;
string mode = "none";
queue<pair<int,int>>* starting_points;
pthread_mutex_t popLock;
pthread_mutex_t countLock;
int point_count;
char choice;


/*
 * Main function
 */
int main(int argc, char *argv[]) {

	/* check args */
	Usage(argc);

	/* get args */
	int size, lines, thread_count;
	mode = argv[1];
	Get_args(size, lines, thread_count);

	/* open output */
    ofstream output;
    output.open("output.ppm");
    if (!output.is_open()) {
        cout << "Error opening file" << endl;
        return 1;
    }

	/* open input for load mode*/
	string inputName;
	ifstream input;
	if (mode == "load") {
		cin >> inputName;
    	input.open(inputName);
	}

    // hardcode mode (could work with rectangles but not implemented)
    rows = size;
    cols = size;

    /* create matrix on heap */
	struct timeval begin_time_matrix, end_time_matrix;
   	gettimeofday(&begin_time_matrix, 0);
	if (mode == "grid") {
		matrix = Create_blank_matrix(thread_count);
	}
	else if (mode == "load") {
		matrix = Create_contour_matrix(thread_count, input);
	}

	/* report time taken to create blank matrix */
	gettimeofday(&end_time_matrix, 0);
	long seconds = end_time_matrix.tv_sec - begin_time_matrix.tv_sec;
  	long microseconds = end_time_matrix.tv_usec - begin_time_matrix.tv_usec;
   	double timeElapsed = seconds + microseconds*1e-6;
	if (mode == "grid") {
   		cout<<"Time taken by "<<thread_count<<" threads to construct blank matrix: "<<timeElapsed<<" seconds"<<endl;
	}
	
	/* generate lines for grid mode and report time taken*/
	struct timeval begin_time_grid, end_time_grid;
	if (mode == "grid") {
		gettimeofday(&begin_time_grid, 0);
		Generate_grid(lines, thread_count);
	
		gettimeofday(&end_time_grid, 0);
		seconds = end_time_grid.tv_sec - begin_time_grid.tv_sec;
		microseconds = end_time_grid.tv_usec - begin_time_grid.tv_usec;
		timeElapsed = seconds + microseconds*1e-6;
		cout<<"Time taken by "<<thread_count<<" threads to produce grid: "<<timeElapsed<<" seconds"<<endl;
	}

	/* coloring circles function */
	pthread_t thread_handles_circles[thread_count];
	struct circle_params circle_args[thread_count];

	/* extract parameters for coloring algorithm from input file */
	starting_points = new queue<pair<int,int>>;
	Get_params(point_count, starting_points);
	
	pthread_mutex_init(&popLock, NULL);
	pthread_mutex_init(&countLock, NULL);

	struct timeval begin_time_color, end_time_color;
   	gettimeofday(&begin_time_color, 0);
	for (int i = 0; i < thread_count; i++) {
		pthread_create(&thread_handles_circles[i], NULL, Spreading_circles, &circle_args[i]);
	}

	/* join threads */
	for (int i = 0; i < thread_count; i++) {
		pthread_join(thread_handles_circles[i], NULL);
	}

	/* report time of the coloring */
	gettimeofday(&end_time_color, 0);
	seconds = end_time_color.tv_sec - begin_time_color.tv_sec;
  	microseconds = end_time_color.tv_usec - begin_time_color.tv_usec;
   	timeElapsed = seconds + microseconds*1e-6;
   	cout<<"Time taken by "<<thread_count<<" threads to fill image: "<<timeElapsed<<" seconds"<<endl;
	delete starting_points;

	/* produce output with contents from matrix */
    Matrix_to_file(output);

	/* delete matrix */
	Delete_matrix();

	return 0;
}

/*----------------------------------------------------------------------------
 * Prints usage information
 */
void Usage(int argnum) {
	if (argnum != 2) {
		cout << "Usage: ./flood <mode: grid/load>" << endl;
		exit(1);
	}
	return;
}

/*----------------------------------------------------------------------------
 * Creates blank matrix of size rows x cols with OpenMP
 */
int*** Create_blank_matrix(int thread_count) {
	matrix = new int**[rows];
	#pragma omp parallel for num_threads(thread_count) schedule(dynamic, 10)
	for (int i = 0; i < rows; i++) {
		matrix[i] = new int*[cols];
		for (int j = 0; j < cols; j++) {
			matrix[i][j] = new int[3];
			matrix[i][j][0] = 255;
			matrix[i][j][1] = 255;
			matrix[i][j][2] = 255;
		}
	}
	return matrix;
}

/*----------------------------------------------------------------------------
 * Creates contour matrix of size rows x cols with OpenMP
 */
int*** Create_contour_matrix(int thread_count, ifstream& input) {
	matrix = new int**[rows];
	string dump;
	for (int i = 0; i < 4; i++) {
    	getline(input, dump);
	}
	for (int i = 0; i < rows; i++) {
		matrix[i] = new int*[cols];
		for (int j = 0; j < cols; j++) {
			matrix[i][j] = new int[3];
			input >> matrix[i][j][0];
			input >> matrix[i][j][1];
			input >> matrix[i][j][2];
		}
	}
	return matrix;
}

/*----------------------------------------------------------------------------
 * Deallocates memory for matrix
 */
void Delete_matrix() {
	for (int i = 0; i < rows; i++) {
        for (int j = 0; j < cols; j++) {
            delete[] matrix[i][j];
        }
        delete[] matrix[i];
    }
    delete[] matrix;
	return;
}

/*----------------------------------------------------------------------------
 * Writes contents of matrix to file
 */
void Matrix_to_file(ofstream &outfile) {
	cout<<"writing to file now"<<endl;
	if (outfile.is_open()) {
        outfile << "P3" << endl;
        outfile << rows << " " << cols << endl;
        outfile << "255" << endl;
        for (int i = 0; i < rows; i++) {
            for (int j = 0; j < cols; j++) {
                outfile << matrix[i][j][0] << " " << matrix[i][j][1] << " " << matrix[i][j][2] << endl;
            }
            outfile << endl;
        }
        outfile.close();
    }
	else {
		cout << "File isn't open" << endl;
		exit(1);
	}
	return;
}

/*----------------------------------------------------------------------------
 * Generates a wiggly line in matrix
 */
void* Generate_line(void* line_gen_args) {
	/* unpack args */
	struct line_gen_params* params = (struct line_gen_params*)line_gen_args;
	int*** matrix = params->matrix;
	int rows = params->rows;
	int cols = params->cols;
	unsigned int thread = params->my_id;
	int workload = params->workload;
	srandom(time(NULL)^thread);

	/* choose wall to start on */
	for (int i = 0; i < workload; i++) {
		int row; 
		int col;
		int wall = (int)random() % (3 + 1 - 0) + 0; //used to be 4
		int offset = (int)random() % ((rows - 1) + 1 - 0) + 0; //0 min, 'rows' max

		if(wall==3){//start on left
			col = 0;
			row = offset;
		}
		else if(wall==2){//start on top
			col = offset;
			row = 0;
		}
		else if(wall==1){//start on right
			col = cols-1;
			row = offset;
		}
		else{		//start on bottom
			col = offset;
			row = rows-1;
		}

		/* generate line */
		int finished=0;
		int counter=0;
		while(finished==0){
		int r=0;
		int b=0;
		int g=0;
		matrix[row][col][0] = r;
		matrix[row][col][1] = g;
		matrix[row][col][2] = b; 
		
		int direction = (int)rand() % (3 + 1 - 0) + 0;           
						
		if(wall==0){//never row++ (bottom start?)
			if(direction==0){///slight turn right
				row--;
				col++;
			}
			if(direction==1){//continue straight
				row--;
			}
			if(direction==2){//slight turn left
				row--;
				col--;
			}

		}
		if(wall==1){//never col++ (right)
			if(direction==0){///slight turn right
				col--;
				row++;
			}
			if(direction==1){//continue straight
				col--;
			}
			if(direction==2){//slight turn left
				col--;
				row--;
			}
		}
		if(wall==2){//never row-- (top start)
			if(direction==0){///slight turn right
				row++;
				col--;
			}
			if(direction==1){//continue straight
				row++;
			}
			if(direction==2){//slight turn left
				row++;
				col++;
			}
		}
		if(wall==3){//never coll-- (left start)
			if(direction==0){///slight turn right
				col++;
				row--;
			}
			if(direction==1){//continue straight
				col++;
			}
			if(direction==2){//slight turn left
				col++;
				row++;
			}
		}
		counter++;
		
		if(row == -1 || row == rows || col == -1 || col == cols)
			finished=1;
		}
	}
	return NULL;
}

/*----------------------------------------------------------------------------
 * Checks if a pixel is to be colored
 */
bool isValid(int row, int col, int red, int green, int blue) {
	if(row>rows-1 || row<0 || col>cols-1 || col<0){
		return false;
	}
	int r = matrix[row][col][0];
	int g = matrix[row][col][1];
	int b = matrix[row][col][2];

	if(!(r == 0 && g == 0 && b == 0)){
        if(!(r == red && g == green && b == blue)){ // HARDCODE AS FUCK
        	return true;
        }
	}
	return false;
}

/*----------------------------------------------------------------------------
 * Resolves thread conflicts over regions of the matrix
 */
bool shouldIQuit(int row, int col, int my_red, int my_green, int my_blue, int orig_red, int orig_green, int orig_blue){
	int r = matrix[row][col][0];
	int g = matrix[row][col][1];
	int b = matrix[row][col][2];
	if(!(r==orig_red && g==orig_green && b==orig_blue)){
		if(my_red<r){
			return true;
		}

	}
	return false;	
}

/*----------------------------------------------------------------------------
 * Coloring with circles
 */
void* Spreading_circles(void* circle_gen_args) {
	while(point_count>0){
		pthread_mutex_lock(&countLock);
		pair<int,int> myStart;
		if(point_count<=0) {
			break;
		}

		if(choice=='n'){
			myStart = starting_points->front();
			starting_points->pop();
		}
		else{
			myStart.first = (int)random() % ((rows - 1) + 1 - 0) + 0;
			myStart.second = (int)random() % ((rows - 1) + 1 - 0) + 0;
		}

		int RED = (((int)random()) % 253)+1;
		int GRE = (((int)random()) % 253)+1;
		int BLU = (((int)random()) % 253)+1;
		int orig_red = matrix[myStart.first][myStart.second][0];
		int orig_green = matrix[myStart.first][myStart.second][1];
		int orig_blue = matrix[myStart.first][myStart.second][2];
		if ((matrix[myStart.first][myStart.second][0] == 0 && 
			matrix[myStart.first][myStart.second][1] == 0 &&
			matrix[myStart.first][myStart.second][2] == 0)||(
			orig_red == RED && 
			orig_green == GRE &&
			orig_blue == BLU)) {
			pthread_mutex_unlock(&countLock);
			continue;
		}
		point_count--;
		pthread_mutex_unlock(&countLock);

		unsigned int *mystate = (unsigned int*)circle_gen_args;
	    *mystate = time(NULL) ^ (long)pthread_self();
		
	    matrix[myStart.first][myStart.second][0] = RED;
	    matrix[myStart.first][myStart.second][1] = GRE;
	    matrix[myStart.first][myStart.second][2] = BLU;
	    queue<pair<int,int>>* q = new queue<pair<int,int>>;
	    q->push(myStart);

	    while(!q->empty()){
	        //pop
	        pair<int,int> curr = q->front();
	        q->pop();
	        
	        //add neighbors
	        if (isValid(curr.first + 1, curr.second, RED, GRE, BLU)) {
	        	if(shouldIQuit(curr.first + 1, curr.second, 
					RED, GRE, BLU, orig_red, orig_green, orig_blue)){
	        		break;///another stronger color already fighting for this reg
	        	}
	            q->push(make_pair(curr.first + 1, curr.second));
	            matrix[curr.first + 1][curr.second][0] = RED;
	            matrix[curr.first + 1][curr.second][1] = GRE;
	            matrix[curr.first + 1][curr.second][2] = BLU;
	        }
	        if (isValid(curr.first - 1, curr.second, RED, GRE, BLU)) {
	        	if(shouldIQuit(curr.first - 1, curr.second, 
					RED, GRE, BLU, orig_red, orig_green, orig_blue)){
	        		break;///another stronger color already fighting for this reg
	        	}
	            q->push(make_pair(curr.first - 1, curr.second));
	            matrix[curr.first - 1][curr.second][0] = RED;
	            matrix[curr.first - 1][curr.second][1] = GRE;
	            matrix[curr.first - 1][curr.second][2] = BLU;
	        }
	        if (isValid(curr.first, curr.second + 1, RED, GRE, BLU)) {
	        	if(shouldIQuit(curr.first, curr.second + 1,
					RED, GRE, BLU, orig_red, orig_green, orig_blue)){
	        		break;///another stronger color already fighting for this reg
	        	}
	            q->push(make_pair(curr.first, curr.second + 1));
	            matrix[curr.first][curr.second + 1][0] = RED;
	            matrix[curr.first][curr.second + 1][1] = GRE;
	            matrix[curr.first][curr.second + 1][2] = BLU;
	        }
	        if (isValid(curr.first, curr.second - 1, RED, GRE, BLU)) {
	        	if(shouldIQuit(curr.first, curr.second - 1,
					RED, GRE, BLU, orig_red, orig_green, orig_blue)){
	        		break;///another stronger color already fighting for this reg
	        	}
	            q->push(make_pair(curr.first, curr.second - 1));
	            matrix[curr.first][curr.second - 1][0] = RED;
	            matrix[curr.first][curr.second - 1][1] = GRE;
	            matrix[curr.first][curr.second - 1][2] = BLU;
	        }
	    }
	    while(!q->empty()){
	    	q->pop();
	    }
		delete q;
	}
	
	return NULL;
}

/*----------------------------------------------------------------------------
 * Get arguments
 */
void Get_args(int &size, int &lines, int &thread_count) {
	if (mode == "grid") {
		cin >> size;
		cin >> lines;
		cin >> thread_count;
	} else if (mode == "load") {
		cin >> size;
		cin >> thread_count;
	} else {
		cout << "Invalid mode" << endl;
		exit(1);
	}
}

/*----------------------------------------------------------------------------
 * Generates the grid
 */
void Generate_grid(int lines, int thread_count) {
	/* generate lines with pthreads for grid mode */
	pthread_t thread_handles_grid[thread_count];
	struct line_gen_params line_gen_args[thread_count];
	for (int i = 0; i < thread_count; i++) {
		line_gen_args[i].matrix = matrix;
		line_gen_args[i].rows = rows;
		line_gen_args[i].cols = cols;
		line_gen_args[i].my_id = i;
		line_gen_args[i].workload = lines / thread_count;
		if (i < lines % thread_count) {
			line_gen_args[i].workload++;
		}
		pthread_create(&thread_handles_grid[i], NULL, Generate_line, &line_gen_args[i]);
	}

	/* join threads */
	for (int i = 0; i < thread_count; i++) {
		pthread_join(thread_handles_grid[i], NULL);
	}
}

/*----------------------------------------------------------------------------
 * Extracts parameters for colorig algorithm form a text file
 */
void Get_params(int &point_count, queue<pair<int,int>> *starting_points){
	int fill_count;
	cin >> fill_count;
	point_count = fill_count;
	cin >> choice;

	if(choice == 'n'){
		for(int i = 0; i < fill_count; i++){
			pair<int,int> current;
			cin >> current.first;
			cin >> current.second;
			starting_points->push(current);
		}
	}
}