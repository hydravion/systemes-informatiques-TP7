#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <string.h>
#include <netinet/in.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <linux/limits.h>

#define USER_INPUT_MAX_SIZE 1000
#define CWD_BUF_MAX_SIZE 1000
#define ARGV_MAX_SIZE 50
#define HOSTNAME_MAX_SIZE 50

#define RESET   "\033[0m"
#define BLACK   "\033[30m"      /* Black */
#define RED     "\033[31m"      /* Red */
#define GREEN   "\033[32m"      /* Green */
#define YELLOW  "\033[33m"      /* Yellow */
#define BLUE    "\033[34m"      /* Blue */
#define MAGENTA "\033[35m"      /* Magenta */
#define CYAN    "\033[36m"      /* Cyan */
#define WHITE   "\033[37m"      /* White */
#define BOLDBLACK   "\033[1m\033[30m"      /* Bold Black */
#define BOLDRED     "\033[1m\033[31m"      /* Bold Red */
#define BOLDGREEN   "\033[1m\033[32m"      /* Bold Green */
#define BOLDYELLOW  "\033[1m\033[33m"      /* Bold Yellow */
#define BOLDBLUE    "\033[1m\033[34m"      /* Bold Blue */
#define BOLDMAGENTA "\033[1m\033[35m"      /* Bold Magenta */
#define BOLDCYAN    "\033[1m\033[36m"      /* Bold Cyan */
#define BOLDWHITE   "\033[1m\033[37m"      /* Bold White */

#define FG_JOB_RUNNING (1 << 0)
#define BG_JOB_RUNNING (1 << 1)

#define RUN_JOB_IN_FG (1 << 2)
#define RUN_JOB_IN_BG (1 << 3)

pid_t fg_job_global;
pid_t bg_job_global;

int fg_bg_status = 0;
int user_cmd_bg_flag = 0;

int printCWD() {
  /* prints the current working directory as part of the user prompt*/

  static char cwd_buf[CWD_BUF_MAX_SIZE];

  getcwd(cwd_buf, CWD_BUF_MAX_SIZE);

  if (errno == ERANGE) {
    fprintf(stderr, "Error: printCWD() - Current working directory is too large\n");
  }
  else {
    printf(BOLDBLUE "%s$ " RESET, cwd_buf);
  }
  return 0;
}

int printUserAndHostName() {
  static char *username = NULL;
  static char hostname[HOSTNAME_MAX_SIZE];

  // get the username if not already defined
  #ifndef IFNDEF_USERNAME
    username = getlogin();

    if (username == NULL) {
      fprintf(stderr, "printUserAndHostName(): couldn't get username.\n");
    }
    else {
      #define IFNDEF_USERNAME
    }
  #endif

  // get the hostname if not already defined
  #ifndef IFNDEF_HOSTNAME
    if (gethostname(hostname, HOSTNAME_MAX_SIZE) < 0) {
      fprintf(stderr, "printUserAndHostName(): couldn't get hostname.\n%s\n", strerror(errno));
    }
    else {
      #define IFNDEF_HOSTNAME
    }
  #endif

  // if both are defined print them as part of the prompt
  #if defined(IFNDEF_HOSTNAME) && defined(IFNDEF_USERNAME)
    printf(BOLDGREEN "%s@%s" RESET ":", username, hostname);
  #endif

  return 0;
}

int printPrompt() {
  printUserAndHostName();
  printCWD();

  return 0;
}

char *readUserInput() {
  /* reads the user input from stdin */

  static char user_input[USER_INPUT_MAX_SIZE];
  memset(user_input, 0, sizeof(char) * USER_INPUT_MAX_SIZE);  // (re)initialize the
                                                              // user_input array

  printPrompt();

  if (fgets(user_input, USER_INPUT_MAX_SIZE, stdin) == NULL) {
    fprintf(stderr, "readUserInput(): an error occured while reading user input.\n");
  }
  user_input[strcspn(user_input, "\n")] = 0;  // snippet found on stackoverflow,
                                              // removes the trailing newline character
                                              // so that strcmp works correctly later on.

  return user_input;
}

void parseAmpersand(char **argv, int argc, int argv_size) {
  if (strcmp(argv[argc-1], "&") == 0) {
    // reallocarray(argv, argc-1, sizeof(char*)); // si alloc dynamique
    // free(argv[argc-1]);
    argv[argc-1] = NULL;
    user_cmd_bg_flag |= RUN_JOB_IN_BG;
  }
}

char **parseInput(char *user_input) {
  /* parses the user input to produce an argv array containing the different
    strings from the input */

  static char *argv[ARGV_MAX_SIZE]; // the argv array
  memset(argv, 0, sizeof(char*) * ARGV_MAX_SIZE); // (re)initialize the
                                                  // user_input array

  char *str, *token;
  int j, argc;

  // inspired by man strtok
  for (j = 0, str = user_input ; ; j++, argc++, str = NULL) {
    token = strtok(str, " \t"); // split the input string (delim bytes are whitespace and tab)
    if (token == NULL)
      break;

    argv[j] = token; // each argv element contains a string
  }

  parseAmpersand(argv, argc, ARGV_MAX_SIZE); // deal with the "&" char for bg jobs

  return argv;
}


int isCmdABuiltin(char **argv) {
  /* determines whether the user's command is a builtin */

  static char *builtins[] = {
    "cd",
    "exit"
  };

  int i;
  int array_size = sizeof(builtins) / sizeof(char*);  // TODO? Change this if
                                                      // array is dynamically
                                                      // allocated later on

  for (i = 0 ; i < array_size ; i++) {
    if (strcmp(argv[0], builtins[i]) == 0) // if the command is a builtin
      return 1;
  }

  return 0;
}

int evalBuiltin(char **argv) {
  /* evaluates builtins */

  if (strcmp(argv[0], "cd") == 0) {
      chdir(argv[1]);
    }
  else if (strcmp(argv[0], "exit") == 0) {
    raise(SIGHUP);
    exit(EXIT_SUCCESS); // TODO: deal with background jobs and signals
  }
  else {
    fprintf(stderr, "evalBuiltin(): builtin not implemented.\n");
  }

  return -1;
}

/* TODO TODO TODO TODO TODO TODO TODO TODO TODO TODO TODO TODO TODO TODO TODO

Demander à Jérémie de tester ton programme pour avoir son feedback

Checker les TODOs du fichiers

TODO
Si je fais
  sleep 10 &
  ls
  ls
Je recois SIGCHLD, et ca fait un waitpid qui bloque la REPL ?
  Comment éviter l'attente du waitpid ? Je recois SIGCHLD avant la fin du
  processus enfant..
TODO

Résoudre problème du sleep() avec bg jobs
  Masquer SIGCHLD ? Vérifier s'il est là entre deux REPL ?

Vérifier que le programme fonctionne correctement
  Faire bg jobs correctement
  Utiliser des structs ? Chaque job a des caractéristiques comme run in fg ou bg etc..

Factoriser le code et
          /!\ séparer en plusieurs fichiers /!\

TODO TODO TODO TODO TODO TODO TODO TODO TODO TODO TODO TODO TODO TODO TODO */

int jobMustRunInFg() {
  return !(user_cmd_bg_flag & RUN_JOB_IN_BG);
}

int jobMustRunInBg() {
  return (user_cmd_bg_flag & RUN_JOB_IN_BG);
}

int isFgJobRunning() {
  return (fg_bg_status & FG_JOB_RUNNING);
}

int isBgJobRunning() {
  return (fg_bg_status & BG_JOB_RUNNING);
}

int runJobInFg(pid_t pid) {
  pid_t w;
  int wstatus;
  fg_job_global = pid; // store the fg job pid
  fg_bg_status |= FG_JOB_RUNNING; // set the flag (fg job is running)
  user_cmd_bg_flag &= ~RUN_JOB_IN_FG;

  w = waitpid(pid, &wstatus, 0); // wait for the child to finish
  if (WIFEXITED(wstatus)) {
    fg_bg_status &= ~FG_JOB_RUNNING; // remove the flag
    return w;
  }
  else {
    fprintf(stderr, "evalJob(): waitpid(): child didn't terminate normally.\n");
    return -1;
  }
}

int runJobInBg() {
  if (isBgJobRunning()) {
    fprintf(stderr, "evalJob(): cannot run two background jobs at the same time.\n");
    return -1;
  }
  fg_bg_status |= BG_JOB_RUNNING; // set the flag (bg job is running)
  user_cmd_bg_flag &= ~RUN_JOB_IN_BG;
  sleep(1); // TODO: enlever sleep. Sinon bg job écrit au mauvais moment
  return -1;
}

void redirectChildStdinToDevNull() {
  /* redirect the child's stdin to /dev/null */

  close(STDIN_FILENO); // TODO needed?
  int fd_dev_null = open("/dev/null", O_RDWR); // found on the internet
  dup2(fd_dev_null, STDIN_FILENO);
  close(fd_dev_null); // TODO needed?
}

int evalJob(char **argv) {
  /* evalJob evaluates a command to launch a job in the foreground or the background. */

  // inspired by the internet, man waitpid

  if (jobMustRunInBg() && isBgJobRunning()) {
    fprintf(stderr, "evalJob(): two background jobs cannot run at the same time.\n");
    user_cmd_bg_flag &= ~RUN_JOB_IN_BG;
    return -1;
  }

  pid_t pid;
  pid = fork();

  if (pid == -1) { // ERROR
    fprintf(stderr, "evalJob(): failed to fork().\n");
  }

  else if (pid == 0) { // RAN BY CHILD

    if (jobMustRunInBg()) {
      redirectChildStdinToDevNull();
  }
    if (execvpe(argv[0], argv, NULL) == -1) {
      fprintf(stderr, "execvpe: %s\n", strerror(errno));
      exit(EXIT_FAILURE);
    }
  }

  else { // RAN BY PARENT

    if (jobMustRunInFg()) { // !(user_cmd_bg_flag & RUN_JOB_IN_BG) ...... && (user_cmd_bg_flag & RUN_JOB_IN_FG) ?
      return runJobInFg(pid);
    }
    else if (jobMustRunInBg()) { // && !(user_cmd_bg_flag & RUN_JOB_IN_FG) ?
      return runJobInBg();
    }
    else {
      fprintf(stderr, "evalJob(): job must run in the foreground or in the background\n");
    }
  }

  return -1;
}


int evalInput(char *user_input) {
  /*  evaluates the user input. It checks for a builtin and evaluates it accordingly
      if one is found. Otherwise it evaluates the command as a job. */

  char **argv;

  argv = parseInput(user_input);

  if (isCmdABuiltin(argv)) {
    evalBuiltin(argv);
  }
  else {
    return evalJob(argv);
  }
  // TODO: gérer commandes non-valides

  return -1;
}

int printResult(pid_t pid_child) {
  /* ?? prints Foreground job exited ?.. */
  if (pid_child < 0) {
    return 0;
  }
  printf(CYAN "\tshello: Foreground job exited with code %d.\n" RESET, pid_child);

  return 0;
}
void printWelcomeMessage() {
  printf(BOLDYELLOW "Welcome to the...\n\n");
  printf(BOLDMAGENTA
  "      ___           ___           ___           ___       ___       ___     \n"
  "     /\\  \\         /\\__\\         /\\  \\         /\\__\\     /\\__\\     /\\  \\    \n"
  "    /::\\  \\       /:/  /        /::\\  \\       /:/  /    /:/  /    /::\\  \\   \n"
  "   /:/\\ \\  \\     /:/__/        /:/\\:\\  \\     /:/  /    /:/  /    /:/\\:\\  \\  \n"
  "  _\\:\\~\\ \\  \\   /::\\  \\ ___   /::\\~\\:\\  \\   /:/  /    /:/  /    /:/  \\:\\  \\ \n"
  " /\\ \\:\\ \\ \\__\\ /:/\\:\\  /\\__\\ /:/\\:\\ \\:\\__\\ /:/__/    /:/__/    /:/__/ \\:\\__\\\n"
  " \\:\\ \\:\\ \\/__/ \\/__\\:\\/:/  / \\:\\~\\:\\ \\/__/ \\:\\  \\    \\:\\  \\    \\:\\  \\ /:/  /\n"
  "  \\:\\ \\:\\__\\        \\::/  /   \\:\\ \\:\\__\\    \\:\\  \\    \\:\\  \\    \\:\\  /:/  / \n"
  "   \\:\\/:/  /        /:/  /     \\:\\ \\/__/     \\:\\  \\    \\:\\  \\    \\:\\/:/  /  \n"
  "    \\::/  /        /:/  /       \\:\\__\\        \\:\\__\\    \\:\\__\\    \\::/  /   \n"
  "     \\/__/         \\/__/         \\/__/         \\/__/     \\/__/     \\/__/    \n"
  RESET);
  printf(BOLDCYAN "\n\nshell.\nEnjoy your stay! :)\n" RESET);
}

void redirectSignalToForegroundJob(int sig) {
  /* redirects a signal to the foreground job */

  if (isFgJobRunning()) {
    kill(fg_job_global, sig); // TODO: how to implement it without using global variables ?
    fg_bg_status &= ~FG_JOB_RUNNING;
  }

}

void reapBackgroundJob(int sig) {
  /* redirects a signal to the foreground job */

  int wstatus, w;

  if (isBgJobRunning()) { /* TODO TODO TODO Ca bloque avec un bg job à cause du waitpid, utiliser WNOHANG ? TODO TODO TODO */
                                              // TODO: comparer pid du processus avec pid du bg job
    w = waitpid(bg_job_global, &wstatus, 0);  // TODO: how to implement it without using global variables ?
                                              // Use sa_sigaction to determine the bg_job's pid ?
    fg_bg_status &= ~BG_JOB_RUNNING; // remove the flag
    printf(CYAN "\tshello: Background job exited with code %d.\n" RESET, w);
  }
}

void redirectSignalToAllJobs(int sig) {
  /* redirects a signal to the foreground job */

  if (isFgJobRunning()) {
    kill(fg_job_global, sig); // TODO: how to implement it without using global variables ?
  }
  if (isBgJobRunning()) {
    kill(bg_job_global, sig);
  }
}

void createSigaction(int sig, void (*my_sa_handler)(int), int flags) {
  struct sigaction sa; // found on the internet
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = flags;
  sa.sa_handler = my_sa_handler;

  sigaction(sig, &sa, NULL);
}

void configureShell() {
  /* specifies how to handle certain signals */

  // Ignore the SIGTERM and SIGQUIT signals
  createSigaction(SIGQUIT, SIG_IGN, SA_RESTART);
  createSigaction(SIGTERM, SIG_IGN, SA_RESTART); // TODO flags.. ??

  // Redirect SIGINT to the foreground job, if one is running
  createSigaction(SIGINT, redirectSignalToForegroundJob, SA_RESTART);
  // Redirect SIGHUP to all jobs
  createSigaction(SIGHUP, redirectSignalToAllJobs, SA_RESTART); // TODO flag ??

  // Properly reap a finished background job
  createSigaction(SIGCHLD, reapBackgroundJob, SA_RESTART);

}

int runReadEvalPrintLoop() {
  /* reads the user input, evaluates it and prints the result of it. */

  char *user_input;
  pid_t pid_child;

  printWelcomeMessage();

  do {
    user_input = readUserInput();
    pid_child = evalInput(user_input);
    printResult(pid_child);
  } while(1);

  return 0;
}

int main(int argc, char *argv[]) {

  configureShell();

  runReadEvalPrintLoop(); // Starts the REPL

  exit(EXIT_SUCCESS);
}
