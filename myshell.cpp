#include <unistd.h>
#include <iostream>
#include <sys/wait.h>
#include <cstring>
#include <fcntl.h>

// Expects the following of input:
// 	0 or 1 of each of the following operators: >, >>, <
// 	Maximum 3 pipe commands, in other words up to 2 of the | operator
// 	Maximum length of maxBytes-1 for each set of commands separated by a semicolon
// 	Maximum maxBytes number of command sets separated by semicolons
// 	No semicolon at the end of the entire input line
//
// Wrote this whole thing in about 5 days - please ignore commented out couts.
// To implement more than 1 prompt for input, entire main method (after taking initial input line) must be in a while loop,
// checking for exit or empty line.
// Better way of task parsing/management/scheduling needed to accept more argument variations.

int main(int argc, char *argv[]) {

    int maxBytes = 100;

    // Take input from user
    char cwd[maxBytes] = {};
    getcwd(cwd, sizeof(cwd));
    std::cout << cwd << "$ " << std::flush;
    char line[maxBytes] = {};
    std::cin.getline(line, sizeof(line));
    //std::cout << "line = " << line << "\n";

    // Start parsing

    // Make 2D array with each row being a command, separate row contents by encountered semicolons
    char *args[maxBytes][maxBytes];
    for(int row = 0; row < maxBytes; row++) {
        for(int col = 0; col < maxBytes; col++) {
            args[row][col] = nullptr;
        }
    }

    // Break line into rows of executable commands
    // If semicolon or operator encountered (i.e. >, <, >>, |), then
    // add the cmds before/after it to a matching I/O array (e.g. pipeArgs), then next row

    // Arrays storing commands to be used with I/O redirection
    // A row for each set of commands separated by semicolons, columns store commands
    int pipeArgs[maxBytes][3]; // {Arg1, arg2, arg3};
    int outArgs[maxBytes][2]; // {From arg1, to file}
    int inArgs[maxBytes][2]; // {To file, from arg1}
    int appArgs[maxBytes][2]; // {From arg1, to file}
    int normalArgs[maxBytes]; // Stores rows where first normal command in a set is
    for(int r = 0; r < maxBytes; r++) {
        for(int c = 0; c < 2; c++) {
            pipeArgs[r][c] = -1;
            outArgs[r][c] = -1;
            inArgs[r][c] = -1;
            appArgs[r][c] = -1;
        }
        pipeArgs[r][2] = -1;
        normalArgs[r] = -1;
    }

    int r = 0; // args row iterator
    int c = 0; // args column iterator
    int k = 0; // pipeArgs iterator to find next available spot
    int t = 0; // I/O redirect arrays iterator
    bool found = false; // Set to true when semicolon or operator found
    int sCount = 0; // Semicolon counter
    char *arg = strtok(line, " "); // Strtok turns included delimiter into null char automatically

    while(arg != nullptr) {
        // Search arg for semicolon
        for(int i = 0; *(arg+i) != '\0'; i++) {
            if(*(arg+i) == ';') { // ; found, set current command and increment arg rows
                found = true;
                t++;
                sCount++;
                strncpy(arg+i, "\0", 1);
                args[r++][c] = arg;
                //std::cout << "Set arg[" << r-1 << "][" << c << "] to " << args[r-1][c] << "\n";
                c = 0;
                k = 0;
            }
        }
        if(!found) { // Other operators might still be in args
            if(*arg == '>') {
                if(strcmp(arg, ">>") == 0) { // Case: append
                    appArgs[t][0] = r;
                    appArgs[t][1] = r+1;
                    r++;
                    c = 0;
                }
                else { // Case: output redirect
                    outArgs[t][0] = r;
                    outArgs[t][1] = r+1;
                    r++;
                    c = 0;
                }
            }
            else if(*arg == '<') { // Case: input redirect
                inArgs[t][0] = r;
                inArgs[t][1] = r+1;
                r++;
                c = 0;
            }
            else if(*arg == '|') { // Case: pipe
                if(k == 0) {
                    pipeArgs[t][k++] = r;
                    pipeArgs[t][k++] = r+1;
                }
                else {
                    pipeArgs[t][k] = r+1;
                }
                r++;
                c = 0;
            }
            else { // Case: command, command argument, or filename
                if(normalArgs[t] == -1) {
                    normalArgs[t] = r;
                }
                args[r][c++] = arg;
                //std::cout << "Set arg[" << r << "][" << c-1 << "] to " << args[r][c-1] << "\n";
            }
        }
        else {
            found = false;
        }
        arg = strtok(NULL, " "); // Next token
    }

    // Edge cases
    normalArgs[0] = 0; // Set first command set
    sCount++; // Count last set without semicolon after it

    // Debugging
    /*for(int r = 0; r < 10; r++) {
        for(int c = 0; c < 2; c++) {
            std::cout << "pipeArgs[" << r << "][" << c << "] = " << pipeArgs[r][c] << "\n";
            std::cout << "inArgs[" << r << "][" << c << "] = " << inArgs[r][c] << "\n";
            std::cout << "outArgs[" << r << "][" << c << "] = " << outArgs[r][c] << "\n";
            std::cout << "appArgs[" << r << "][" << c << "] = " << appArgs[r][c] << "\n";
        }
        std::cout << "pipeArgs[" << r << "][2] = " << pipeArgs[r][2] << "\n";
    }*/

    // Execute each row of args, resetting all fds when semicolon found at the end of a row

    //std::cout << "Entering last for loop.\n";

    int bufferSize = 512;

    for(int u = 0; u < sCount; u++) {

        //std::cout << "Starting set " << u << " of commands.\n";

        int fd[2]; // Stores created file descriptors (i.e. {Input, output}, pipe-like)
        if(pipe(fd) < 0) {
            perror("Pipe creation error.\n");
            return 1;
        }
        int fd2[2]; // Second pipe b/c you can't read multiple times from the same pipe (??)
        if(pipe(fd2) < 0) {
            perror("Pipe creation error.\n");
        }
        bool readFromFd2 = false; // Default result is read from fd[0], set to true if 2 pipe commands used

        // Input redirect
        int inputFd = -1;
        if(inArgs[u][0] != -1) {
            inputFd = open(args[inArgs[u][1]][0], O_RDONLY);
            //std::cout << "setting inputFd to " << args[inArgs[u][1]][0] << "\n";
        }

        //std::cout << "Successfully redirected input.\n";

        // Normal or pipe command execution, leaving all pipes open
        if(pipeArgs[u][0] == -1) { // Normal command

            //std::cout << "Executing normal command.\n" << std::flush;

            int pid = fork();
            if(pid < 0) {
                perror("Fork normal cmd error.\n");
                return 6;
            }
            else if(pid == 0) {
                if(inArgs[u][1] != -1) { // If input redirect
                    dup2(inputFd, STDIN_FILENO); // All input is now from inputFd
                    if(close(inputFd) < 0) {
                        perror("Close input fd after exec error.\n");
                        return 5;
                    }
                }
                dup2(fd[1], STDOUT_FILENO); // All output now goes to fd[1]

                // Search for location of cmd in args
                int cmdRow = -1; // Stores correct args row to execute
                if(inArgs[u][1] != -1) {
                    cmdRow = inArgs[u][0];
                }
                else if(outArgs[u][0] != -1) {
                    cmdRow = outArgs[u][0];
                }
                else if(appArgs[u][0] != -1) {
                    cmdRow = appArgs[u][0];
                }
                else {
                    cmdRow = normalArgs[u];
                }

                // Close pipe in child
                if(close(fd[0]) < 0) {
                    perror("Close pipe read fd in normal cmd exec error.\n");
                    return 5;
                }
                if(close(fd[1]) < 0) {
                    perror("Close pipe write fd in normal cmd exec error.\n");
                    return 5;
                }

                if(execvp(args[cmdRow][0], args[cmdRow]) < 0) {
                    perror("Execute normal cmd error.\n");
                    return 7;
                }
            }
            // Close input fd if it exists
            if(inArgs[u][1] != -1) {
                if(close(inputFd) < 0) {
                    perror("Close input fd after exec error.\n");
                    return 5;
                }
            }
        }
        else { // Pipe commands

            // Command 1

            //std::cout << "Executing pipe command 1.\n";

            int pid = fork();
            if(pid < 0) {
                perror("Fork pipe cmd error.\n");
                return 6;
            }
            else if(pid == 0) {
                if(inArgs[u][1] != -1) { // If input redirect
                    dup2(inputFd, STDIN_FILENO); // All input is now from inputFd
                    if(close(inputFd) < 0) {
                        perror("Close input fd after exec error.\n");
                        return 5;
                    }
                }
                dup2(fd[1], STDOUT_FILENO); // All output now goes to fd[1]
                // Close pipe in child
                if(close(fd[0]) < 0) {
                    perror("Close pipe read fd in pipe cmd exec error.\n");
                    return 5;
                }
                if(close(fd[1]) < 0) {
                    perror("Close pipe write fd in pipe cmd exec error.\n");
                    return 5;
                }
                if(close(fd2[0]) < 0) {
                    perror("Close pipe read fd2 in pipe cmd exec error.\n");
                    return 5;
                }
                if(close(fd2[1]) < 0) {
                    perror("Close pipe write fd2 in pipe cmd exec error.\n");
                    return 5;
                }
                if(execvp(args[pipeArgs[u][0]][0], args[pipeArgs[u][0]]) < 0) {
                    perror("Execute pipe cmd error.\n");
                    return 7;
                }
            }

            wait(NULL);

            // Close input fd if it exists
            if(inArgs[u][1] != -1) {
                if(close(inputFd) < 0) {
                    perror("Close input fd after exec error.\n");
                    return 5;
                }
            }

            // Command 2

            //std::cout << "Executing pipe command 2.\n";

            int pid2 = fork();
            if(pid2 < 0) {
                perror("Fork pipe cmd error.\n");
                return 6;
            }
            else if(pid2 == 0) {
                dup2(fd[0], STDIN_FILENO); // All input is now from fd[0]
                dup2(fd2[1], STDOUT_FILENO); // All output now goes to fd2[1]
                // Close pipes in child
                if(close(fd[0]) < 0) {
                    perror("Close pipe read fd in pipe cmd exec error.\n");
                    return 5;
                }
                if(close(fd[1]) < 0) {
                    perror("Close pipe write fd in pipe cmd exec error.\n");
                    return 5;
                }
                if(close(fd2[0]) < 0) {
                    perror("Close pipe read fd2 in pipe cmd exec error.\n");
                    return 5;
                }
                if(close(fd2[1]) < 0) {
                    perror("Close pipe write fd2 in pipe cmd exec error.\n");
                    return 5;
                }
                if(execvp(args[pipeArgs[u][1]][0], args[pipeArgs[u][1]]) < 0) {
                    perror("Execute pipe cmd error.\n");
                    return 7;
                }
            }

            if(pipeArgs[u][2] != -1) {

                // Command 3

                //std::cout << "Executing pipe command 3.\n";

                // Close old pipe
                if(close(fd[0]) < 0) {
                    perror("Close pipe read fd in pipe cmd exec error.\n");
                    return 5;
                }
                if(close(fd[1]) < 0) {
                    perror("Close pipe write fd in pipe cmd exec error.\n");
                    return 5;
                }

                wait(NULL);

                pipe(fd); // Reuse old pipe for new

                //std::cout << "Successfully set up re-used fd for pipe.\n";

                int pid3 = fork();
                if(pid3 < 0) {
                    perror("Fork pipe cmd error.\n");
                    return 6;
                }
                else if(pid3 == 0) {
                    dup2(fd2[0], STDIN_FILENO); // All input is now from fd2[0]
                    dup2(fd[1], STDOUT_FILENO); // All output now goes to fd[1]
                    // Close pipes in child
                    if(close(fd[0]) < 0) {
                        perror("Close pipe read fd in pipe cmd exec error.\n");
                        return 5;
                    }
                    if(close(fd[1]) < 0) {
                        perror("Close pipe write fd in pipe cmd exec error.\n");
                        return 5;
                    }
                    if(close(fd2[0]) < 0) {
                        perror("Close pipe read fd2 in pipe cmd exec error.\n");
                        return 5;
                    }
                    if(close(fd2[1]) < 0) {
                        perror("Close pipe write fd2 in pipe cmd exec error.\n");
                        return 5;
                    }
                    if(execvp(args[pipeArgs[u][2]][0], args[pipeArgs[u][2]]) < 0) {
                        perror("Execute pipe cmd error.\n");
                        return 7;
                    }
                }
            }
            else { // Mark that 2 commands were used, result is in fd2[0]
                readFromFd2 = true;
            }
        }

        // Close pipe write fds
        if(close(fd[1]) < 0) {
            perror("Close pipe write end error.\n");
            return 5;
        }
        if(close(fd2[1]) < 0) {
            perror("Close pipe write end error.\n");
            return 5;
        }

        wait(NULL);

        //std::cout << "Finished executing commands, readFromFd2 = " << readFromFd2 << ".\n";

        // Read from fd[0] or fd2[0]
        char buffer[bufferSize];
        int len = -1;
        if(readFromFd2) {
            len = read(fd2[0], buffer, bufferSize);
        }
        else {
            len = read(fd[0], buffer, bufferSize);
        }
        if(len < 0) { // Read from output
            perror("Read from pipe output after exec error.\n");
            return 3;
        }
        //std::cout << "len = " << len << "\n";

        //std::cout << "Successfully read from pipe input after execution.\n";

        // Close pipe read fds
        if(close(fd[0]) < 0) {
            perror("Close pipe read end error.\n");
            return 5;
        }
        if(close(fd2[0]) < 0) {
            perror("Close pipe read end error.\n");
            return 5;
        }


        //std::cout << "Successfully closed pipe fds.\n";

        // Check for output redirect or append
        int outputFd = -1;
        if(outArgs[u][0] != -1) { // Output redirect
            outputFd = creat(args[outArgs[u][1]][0], S_IRWXU);
            if(outputFd < 0) {
                perror("Creat for output redir error.\n");
                return 8;
            }
        }
        else if(appArgs[u][0] != -1) { // Append
            outputFd = open(args[appArgs[u][1]][0], O_WRONLY | O_APPEND);
            if(outputFd < 0) {
                perror("Open append fd using args filename error.\n");
                return 2;
            }
        }
        else { // Normal output
            outputFd = dup(STDOUT_FILENO);
        }

        //std::cout << "Successfully created new output fd after execution.\n";

        if(write(outputFd, buffer, len) < 0) {
            perror("Write to output buffer after exec error.\n");
            return 4;
        }
        if(close(outputFd) < 0) {
            perror("Close output fd after exec error.\n");
            return 5;
        }

        //std::cout << "Successfully wrote output and closed output fd after execution.\n";
        //std::cout << "Finished set " << u << " of commands.\n";
    }

    //std::cout << "Reached end of program.\n";

    return 0;
}
