/*
 * usage: ./dependencyDiscoverer [-Idir] ... file.c|file.l|file.y ...
 *
 * processes the c/yacc/lex source file arguments, outputting the dependencies
 * between the corresponding .o file, the .c source file, and any included
 * .h files
 *
 * each .h file is also processed to yield a dependency between it and any
 * included .h files
 *
 * these dependencies are written to standard output in a form compatible with
 * make; for example, assume that foo.c includes inc1.h, and inc1.h includes
 * inc2.h and inc3.h; this results in
 *
 *                  foo.o: foo.c inc1.h inc2.h inc3.h
 *
 * note that system includes (i.e. those in angle brackets) are NOT processed
 *
 * dependencyDiscoverer uses the CPATH environment variable, which can contain a
 * set of directories separated by ':' to find included files
 * if any additional directories are specified in the command line,
 * these are prepended to those in CPATH, left to right
 *
 * for example, if CPATH is "/home/user/include:/usr/local/group/include",
 * and if "-Ifoo/bar/include" is specified on the command line, then when
 * processing
 *           #include "x.h"
 * x.h will be located by searching for the following files in this order
 *
 *      ./x.h
 *      foo/bar/include/x.h
 *      /home/user/include/x.h
 *      /usr/local/group/include/x.h
 */

/*
   * general design of main()
   * ========================
   * There are three globally accessible variables:
   * - dirs: a vector storing the directories to search for headers
   * - theTable: a hash table mapping file names to a list of dependent file names
   * - workQ: a list of file names that have to be processed
   *
   * 1. look up CPATH in environment
   * 2. assemble dirs vector from ".", any -Idir flags, and fields in CPATH
   *    (if it is defined)
   * 3. for each file argument (after -Idir flags)
   *    a. insert mapping from file.o to file.ext (where ext is c, y, or l) into
   *       table
   *    b. insert mapping from file.ext to empty list into table
   *    c. append file.ext on workQ
   * 4. for each file on the workQ
   *    a. lookup list of dependencies
   *    b. invoke process(name, list_of_dependencies)
   * 5. for each file argument (after -Idir flags)
   *    a. create a hash table in which to track file names already printed
   *    b. create a linked list to track dependencies yet to print
   *    c. print "foo.o:", insert "foo.o" into hash table
   *       and append "foo.o" to linked list
   *    d. invoke printDependencies()
   *
   * general design for process()
   * ============================
   *
   * 1. open the file
   * 2. for each line of the file
   *    a. skip leading whitespace
   *    b. if match "#include"
   *       i. skip leading whitespace
   *       ii. if next character is '"'
   *           * collect remaining characters of file name (up to '"')
   *           * append file name to dependency list for this open file
   *           * if file name not already in the master Table
   *             - insert mapping from file name to empty list in master table
   *             - append file name to workQ
   * 3. close file
   *
   * general design for printDependencies()
   * ======================================
   *
   * 1. while there is still a file in the toProcess linked list
   * 2. fetch next file from toProcess
   * 3. lookup up the file in the master table, yielding the linked list of dependencies
   * 4. iterate over dependenceies
   *    a. if the filename is already in the printed hash table, continue
   *    b. print the filename
   *    c. insert into printed
   *    d. append to toProcess
   *
   * Additional helper functions
   * ===========================
   *
   * dirName() - appends trailing '/' if needed
   * parseFile() - breaks up filename into root and extension
   * openFile()  - attempts to open a filename using the search path defined by the dirs vector.
   */

#include <ctype.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <condition_variable>
#include <list>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// thread safe thread tracker
struct ThreadTracker {
   private:
    int t_count;
    std::condition_variable cv;
    std::mutex mutex;

   public:
    ThreadTracker(int t_count) {
        this->t_count = t_count;
    }

    void signal_done() {
        std::unique_lock<std::mutex> lock(this->mutex);
        this->t_count--;
        lock.unlock();
        cv.notify_one();
    }

    void wait_done() {
        std::unique_lock<std::mutex> lock(this->mutex);
        while (this->t_count > 0) {
            this->cv.wait(lock);
        }
        // continue with thread
    }
};

// thread safe queue
struct QueueSafe {
   private:
    std::list<std::string> q;
    std::mutex mutex;

   public:
    void push_back(std::string string) {
        std::unique_lock<std::mutex> lock(mutex);
        this->q.push_back(string);
    }

    std::string pop_front() {
        std::unique_lock<std::mutex> lock(mutex);
        if (this->q.empty() == true) {
            return NULL;
        } else {
            std::string s = this->q.front();
            this->q.pop_front();
            return s;
        }
    }

    std::size_t size() {
        std::unique_lock<std::mutex> lock(mutex);
        return this->q.size();
    }

    std::string front() {
        std::unique_lock<std::mutex> lock(mutex);
        if (this->q.empty() == true) {
            return std::string("");
        } else {
            return this->q.front();
        }
    }
};

// thread safe map
struct MapSafe {
   private:
    std::unordered_map<std::string, std::list<std::string>> map;
    std::mutex mutex;

   public:
    std::unordered_map<std::string, std::list<std::string>>::iterator find(std::string string) {
        //std::unique_lock<std::mutex> lock(mutex);
        return this->map.find(string);
    }

    std::unordered_map<std::string, std::list<std::string>>::iterator end() {
        //std::unique_lock<std::mutex> lock(mutex);
        return this->map.end();
    }

    void insert(std::pair<std::string, std::list<std::string>> pair) {
        std::unique_lock<std::mutex> lock(mutex);
        this->map.insert(pair);
    }

    std::list<std::string>* getValue(std::string string) {
        std::unique_lock<std::mutex> lock(mutex);
        return &this->map[string];
    }
};

// Semaphore
struct BinarySemaphore {
   private:
    int val;
    std::condition_variable cv;
    std::mutex mutex;

   public:
    BinarySemaphore() {
        this->val = 1;
    }

    void wait() {
        std::unique_lock<std::mutex> lock(mutex);
        while (this->val <= 0) {
            this->cv.wait(lock);
        }
        this->val--;
    }

    void signal() {
        std::unique_lock<std::mutex> lock(mutex);
        while (this->val > 0) {
            this->cv.wait(lock);
        }
        this->val++;
        lock.unlock();
        this->cv.notify_one();
    }
};

std::vector<std::string> dirs;
MapSafe theTable;
QueueSafe workQ;
BinarySemaphore bs;

std::string dirName(const char* c_str) {
    std::string s = c_str;  // s takes ownership of the string content by allocating memory for it
    if (s.back() != '/') {
        s += '/';
    }
    return s;
}

std::pair<std::string, std::string> parseFile(const char* c_file) {
    std::string file = c_file;
    std::string::size_type pos = file.rfind('.');
    if (pos == std::string::npos) {
        return {file, ""};
    } else {
        return {file.substr(0, pos), file.substr(pos + 1)};
    }
}

// open file using the directory search path constructed in main()
static FILE* openFile(const char* file) {
    FILE* fd;
    for (unsigned int i = 0; i < dirs.size(); i++) {
        std::string path = dirs[i] + file;
        fd = fopen(path.c_str(), "r");
        if (fd != NULL)
            return fd;  // return the first file that successfully opens
    }
    return NULL;
}

// process file, looking for #include "foo.h" lines
static void process(const char* file, std::list<std::string>* ll) {
    char buf[4096], name[4096];
    // 1. open the file
    FILE* fd = openFile(file);
    if (fd == NULL) {
        fprintf(stderr, "Error opening %s\n", file);
        exit(-1);
    }
    while (fgets(buf, sizeof(buf), fd) != NULL) {
        char* p = buf;
        // 2a. skip leading whitespace
        while (isspace((int)*p)) {
            p++;
        }
        // 2b. if match #include
        if (strncmp(p, "#include", 8) != 0) {
            continue;
        }
        p += 8;  // point to first character past #include
        // 2bi. skip leading whitespace
        while (isspace((int)*p)) {
            p++;
        }
        if (*p != '"') {
            continue;
        }
        // 2bii. next character is a "
        p++;  // skip "
        // 2bii. collect remaining characters of file name
        char* q = name;
        while (*p != '\0') {
            if (*p == '"') {
                break;
            }
            *q++ = *p++;
        }
        *q = '\0';
        // 2bii. append file name to dependency list
        bs.wait();
        ll->push_back({name});
        // 2bii. if file name not already in table ...
        if (theTable.find(name) != theTable.end()) {
            bs.signal();
            continue;
        }
        // ... insert mapping from file name to empty list in table ...
        theTable.insert({name, {}});
        // ... append file name to workQ
        workQ.push_back(name);
        bs.signal();
    }
    // 3. close file
    fclose(fd);
}

// iteratively print dependencies
static void printDependencies(std::unordered_set<std::string>* printed,
                              std::list<std::string>* toProcess,
                              FILE* fd) {
    if (!printed || !toProcess || !fd)
        return;

    // 1. while there is still a file in the toProcess list
    while (toProcess->size() > 0) {
        // 2. fetch next file to process
        std::string name = toProcess->front();
        toProcess->pop_front();
        // 3. lookup file in the table, yielding list of dependencies
        std::list<std::string>* ll = theTable.getValue(name);
        // 4. iterate over dependencies
        for (auto iter = ll->begin(); iter != ll->end(); iter++) {
            // 4a. if filename is already in the printed table, continue
            if (printed->find(*iter) != printed->end()) {
                continue;
            }
            // 4b. print filename
            fprintf(fd, " %s", iter->c_str());
            // 4c. insert into printed
            printed->insert(*iter);
            // 4d. append to toProcess
            toProcess->push_back(*iter);
        }
    }
}

int main(int argc, char* argv[]) {
    // 1. look up CPATH in environment
    char* cpath = getenv("CPATH");
    char* crawlerthreads = getenv("CRAWLER_THREADS");
    int number_of_threads;
    if (crawlerthreads == NULL) {
        number_of_threads = 2;
    } else {
        number_of_threads = std::stoi(crawlerthreads);
    }

    // init. setup threads, locks and condition variables
    std::vector<std::thread> threads;
    ThreadTracker tracker(number_of_threads);
    BinarySemaphore threadlock;

    // determine the number of -Idir arguments
    int i;
    for (i = 1; i < argc; i++) {
        if (strncmp(argv[i], "-I", 2) != 0)
            break;
    }
    int start = i;

    // 2. start assembling dirs vector
    dirs.push_back(dirName("./"));  // always search current directory first
    for (i = 1; i < start; i++) {
        dirs.push_back(dirName(argv[i] + 2 /* skip -I */));
    }
    if (cpath != NULL) {
        std::string str(cpath);
        std::string::size_type last = 0;
        std::string::size_type next = 0;
        while ((next = str.find(":", last)) != std::string::npos) {
            dirs.push_back(str.substr(last, next - last));
            last = next + 1;
        }
        dirs.push_back(str.substr(last));
    }
    // 2. finished assembling dirs vector

    // 3. for each file argument ...
    for (i = start; i < argc; i++) {
        std::pair<std::string, std::string> pair = parseFile(argv[i]);
        if (pair.second != "c" && pair.second != "y" && pair.second != "l") {
            fprintf(stderr, "Illegal extension: %s - must be .c, .y or .l\n",
                    pair.second.c_str());
            return -1;
        }

        std::string obj = pair.first + ".o";

        // 3a. insert mapping from file.o to file.ext
        theTable.insert({obj, {argv[i]}});

        // 3b. insert mapping from file.ext to empty list
        theTable.insert({argv[i], {}});

        // 3c. append file.ext on workQ
        workQ.push_back(argv[i]);
    }

    // 4. for each file on the workQ
    for (int i = 0; i < number_of_threads; i++) {
        threads.push_back(std::thread([tracker = &tracker, thread = &threadlock]() {
            while (true) {
                thread->wait();
                if (workQ.size() > 0) {
                    std::string filename = workQ.pop_front();
                    // 4a&b. lookup dependencies and invoke 'process'
                    thread->signal();
                    process(filename.c_str(), theTable.getValue(filename));
                } else {
                    thread->signal();
                    break;
                }
            }
            tracker->signal_done();
        }));
    }

    tracker.wait_done();
    for (auto& thread : threads) {
        if (thread.joinable()) {
            thread.join();
        }
    }

    // 5. for each file argument
    for (i = start; i < argc; i++) {
        // 5a. create hash table in which to track file names already printed
        std::unordered_set<std::string> printed;
        // 5b. create list to track dependencies yet to print
        std::list<std::string> toProcess;

        std::pair<std::string, std::string> pair = parseFile(argv[i]);

        std::string obj = pair.first + ".o";
        // 5c. print "foo.o:" ...
        printf("%s:", obj.c_str());
        // 5c. ... insert "foo.o" into hash table and append to list
        printed.insert(obj);
        toProcess.push_back(obj);
        // 5d. invoke
        printDependencies(&printed, &toProcess, stdout);

        printf("\n");
    }

    return 0;
}
