#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>

#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <dirent.h> 

#include <gdk/gdk.h>
#include <gtk/gtk.h>

// variables
static char* buf = NULL;
static char current_file[PATH_MAX] = "";

static pthread_t* threads = NULL;

static int filethread_halt = 0;
static int clipthread_halt = 0;

static GtkClipboard* clipboard = NULL;

static void cleanup( void );
static void cleanup_signal( int sig );
static void* clipthread_fun( void* pthread_data );
static void* filethread_fun( void* pthread_data );

// settings
static char fifo_name[] = "/tmp/clipboard.fifo";
static char dir_name[] = "/tmp/clipboard.dir";

static size_t filethread_sleeptime = 1024*128;

static size_t clipthread_bufsize = 1024*1024;
static size_t clipthread_sleeptime = 1024*128;

static int clipboard_use_primary = 0;

int main(int argc, char** argv) {

    // handle arguments
    int opt;
    char pad[PATH_MAX];
    sprintf( pad, "%*c", strlen(argv[0]), ' ' );
    while ((opt = getopt(argc, argv, "F:D:s:S:ph")) != -1) {
        switch (opt) {
            case 'F': strcpy(fifo_name, optarg); break;
            case 'D': strcpy(dir_name, optarg); break;
            case 's': filethread_sleeptime = atoi(optarg); break;
            case 'S': clipthread_sleeptime = atoi(optarg); break;
            case 'p': clipboard_use_primary = 1; break;
            case 'h': fprintf(stderr, "usage: %s [-F <fifo file name>] [-D <dir name>]\n"
                                      "       %s [-s <file thread usleep>] [-S <clip thread usleep>]\n"
                                      "       %s [-p: use primary]\n", argv[0], pad, pad);
                      exit(EXIT_SUCCESS); break;
            case '?':
            default:
                      fprintf(stderr, "%s -h for help.\n", argv[0]);
                      exit(EXIT_FAILURE); break;
        }
    }
    argc -= optind;
    argv += optind;

    // initialize gtk
    gtk_init(&argc, &argv);

    // clean up before starting
    remove(dir_name);
    remove(fifo_name);

    // set cleanup on interrupt
    signal(SIGINT, cleanup_signal);
    signal(SIGTERM, cleanup_signal);

    // allocate buffer
    if ((buf = calloc(2,clipthread_bufsize)) == NULL) {
        fprintf(stderr, "clip-server: could not allocate memory\n");
        cleanup();
        return EXIT_FAILURE;
    }

    // make fifo
    int status;
    if ((status = mkfifo(fifo_name, 0644)) != 0) {
        fprintf(stderr, "clip-server: could not create fifo %s (%i)\n", fifo_name, status);
        cleanup();
        return EXIT_FAILURE;
    }
    // make dir
    if ((status = mkdir(dir_name, 0755)) != 0) {
        fprintf(stderr, "clip-server: could not create dir %s (%i)\n", dir_name, status);
        cleanup();
        return EXIT_FAILURE;
    }

    // clipboard
    clipboard = gtk_clipboard_get(clipboard_use_primary ? GDK_SELECTION_PRIMARY : GDK_SELECTION_CLIPBOARD);

    // main loop
    if ((threads = calloc(2, sizeof(pthread_t))) == NULL) {
        fprintf(stderr, "clip-server: could not allocate memory\n");
        cleanup();
        return EXIT_FAILURE;
    }
    pthread_create( threads+0, NULL, &clipthread_fun, NULL );
    pthread_create( threads+1, NULL, &filethread_fun, NULL );
    gtk_main();

    cleanup();
    return EXIT_SUCCESS;
}

static void cleanup( void ) {
    free(buf);
    remove(fifo_name);
    remove(dir_name);
    if (threads != NULL) {
        clipthread_halt = 1;
        filethread_halt = 1;
        pthread_join( threads[0], NULL );
        pthread_join( threads[1], NULL );
        free(threads);
    }
    gtk_main_quit();
}

static void cleanup_signal( int sig ) {
    if (sig == SIGINT || sig == SIGTERM) {
        fprintf(stderr, "\nclip-server: shutting down\n");
        cleanup();
        exit(EXIT_SUCCESS);
    }
}

static void* clipthread_fun( void* pthread_data ) {
    (void)pthread_data;
    while (!clipthread_halt) {
        struct pollfd fd;
        fd.fd = open(fifo_name, O_RDONLY | O_NONBLOCK);
        fd.events = POLLIN;
        poll(&fd, 1, 500);
        if (fd.revents & POLLIN) {
            ssize_t num_bytes = read(fd.fd, buf, clipthread_bufsize);
            if (num_bytes >= 0) {
                gtk_clipboard_set_text( clipboard, buf, num_bytes );
                fprintf(stderr, "clip-server: file %s (%li bytes) on clipboard\n", current_file, num_bytes);
            }
            close(fd.fd);
        }
        usleep(clipthread_sleeptime);
    }
    return NULL;
}

static void* filethread_fun( void* pthread_data ) {
    (void)pthread_data;
    DIR *d;
    struct dirent *dir;
    while (!filethread_halt) {
        d = opendir(dir_name);
        if (d) {
            while ((dir = readdir(d)) != NULL) {
                if (strcmp(dir->d_name, ".") && strcmp(dir->d_name, "..")) {
                    // all files in dir that are 'non-trivial'

                    strcpy( current_file, dir->d_name );

                    char path[PATH_MAX];
                    strcpy( path, dir_name );
                    strcat( path, "/" );
                    strcat( path, current_file );

                    int fd_file = open(path, O_RDONLY);
                    char* buf_file = buf + clipthread_bufsize;
                    ssize_t num_bytes_read = read(fd_file, buf_file, clipthread_bufsize);
                    if (num_bytes_read >= 0) {
                        int fd_fifo = open(fifo_name, O_WRONLY);
                        ssize_t num_bytes_written = write(fd_fifo, buf_file, num_bytes_read);
                        if (num_bytes_written < 0)
                            fprintf(stderr, "clip-server: could not write to fifo\n");
                        else if (num_bytes_written == 0)
                            fprintf(stderr, "clip-server: empty file %s\n", current_file);
                    } else {
                        fprintf(stderr, "clip-server: could not read file %s\n", path);
                    }
                    remove(path);

                }
            }
            closedir(d);
        }
        usleep(filethread_sleeptime);
    }
    return NULL;
}

