#define _GNU_SOURCE

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/reboot.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <time.h>
#include <unistd.h>

#define PROGRAM_NAME "root-usb-watchdog"
#define PROGRAM_VERSION "0.1.0"
#define DEFAULT_DEVICE "/dev/mapper/cryptroot"
#define POLL_INTERVAL_NS 100000000L

static volatile sig_atomic_t stop_requested;

static void usage(FILE *stream)
{
    fprintf(stream,
            "Usage: %s [--check] [--device PATH]\n"
            "       %s --help\n"
            "       %s --version\n\n"
            "Monitor the single block device underlying a dm-crypt mapping and\n"
            "power off immediately if that backing device disappears.\n\n"
            "Options:\n"
            "  --device PATH  Device-mapper block device to monitor\n"
            "                 (default: %s)\n"
            "  --check        Resolve and report the backing device, then exit\n"
            "  --help         Show this help text\n"
            "  --version      Show the program version\n",
            PROGRAM_NAME, PROGRAM_NAME, PROGRAM_NAME, DEFAULT_DEVICE);
}

static void handle_stop_signal(int signal_number)
{
    (void)signal_number;
    stop_requested = 1;
}

static int install_signal_handlers(void)
{
    struct sigaction action = {
        .sa_handler = handle_stop_signal,
    };

    sigemptyset(&action.sa_mask);
    if (sigaction(SIGINT, &action, NULL) < 0 ||
        sigaction(SIGTERM, &action, NULL) < 0) {
        return -1;
    }

    return 0;
}

/*
 * Resolve the one sysfs slave below a device identified by major/minor.
 * sysfs_root is injectable so the resolver can be tested without block devices.
 */
static int resolve_slave_from_numbers(const char *sysfs_root,
                                      unsigned int device_major,
                                      unsigned int device_minor,
                                      char *resolved,
                                      size_t resolved_size)
{
    char slaves_path[PATH_MAX];
    char slave_link[PATH_MAX];
    char canonical[PATH_MAX];
    char slave_name[NAME_MAX + 1] = {0};
    size_t slave_count = 0;
    DIR *directory;
    struct dirent *entry;

    if (snprintf(slaves_path, sizeof(slaves_path), "%s/dev/block/%u:%u/slaves",
                 sysfs_root, device_major, device_minor) >=
        (int)sizeof(slaves_path)) {
        errno = ENAMETOOLONG;
        return -1;
    }

    directory = opendir(slaves_path);
    if (directory == NULL) {
        return -1;
    }

    errno = 0;
    while ((entry = readdir(directory)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        ++slave_count;
        if (slave_count == 1) {
            if (strlen(entry->d_name) > NAME_MAX) {
                (void)closedir(directory);
                errno = ENAMETOOLONG;
                return -1;
            }
            strcpy(slave_name, entry->d_name);
        }
    }

    if (errno != 0) {
        int saved_errno = errno;
        (void)closedir(directory);
        errno = saved_errno;
        return -1;
    }
    if (closedir(directory) < 0) {
        return -1;
    }

    if (slave_count == 0) {
        errno = ENODEV;
        return -1;
    }
    if (slave_count != 1) {
        errno = E2BIG;
        return -1;
    }

    if (snprintf(slave_link, sizeof(slave_link), "%s/%s",
                 slaves_path, slave_name) >= (int)sizeof(slave_link)) {
        errno = ENAMETOOLONG;
        return -1;
    }

    if (realpath(slave_link, canonical) == NULL) {
        return -1;
    }
    if (strlen(canonical) + 1 > resolved_size) {
        errno = ENAMETOOLONG;
        return -1;
    }

    strcpy(resolved, canonical);
    return 0;
}

static int resolve_backing_device(const char *device,
                                  const char *sysfs_root,
                                  char *resolved,
                                  size_t resolved_size)
{
    struct stat device_stat;

    if (stat(device, &device_stat) < 0) {
        return -1;
    }
    if (!S_ISBLK(device_stat.st_mode)) {
        errno = ENOTBLK;
        return -1;
    }

    return resolve_slave_from_numbers(sysfs_root,
                                      major(device_stat.st_rdev),
                                      minor(device_stat.st_rdev),
                                      resolved,
                                      resolved_size);
}

static void request_poweroff(void)
{
    int saved_errno;

    if (reboot(RB_POWER_OFF) == 0) {
        return;
    }

    saved_errno = errno;
    fprintf(stderr, "%s: kernel power-off failed: %s; asking systemd\n",
            PROGRAM_NAME, strerror(saved_errno));
    fflush(stderr);

    (void)kill(1, SIGRTMIN + 4);

    for (;;) {
        (void)reboot(RB_POWER_OFF);
        {
            const struct timespec delay = {.tv_sec = 0, .tv_nsec = POLL_INTERVAL_NS};
            (void)nanosleep(&delay, NULL);
        }
    }
}

#ifndef ROOT_USB_WATCHDOG_NO_MAIN
int main(int argc, char **argv)
{
    const char *device = DEFAULT_DEVICE;
    char watched_path[PATH_MAX];
    bool check_only = false;
    struct stat watched_stat;
    int index;

    for (index = 1; index < argc; ++index) {
        if (strcmp(argv[index], "--check") == 0) {
            check_only = true;
        } else if (strcmp(argv[index], "--device") == 0) {
            if (++index >= argc) {
                fprintf(stderr, "%s: --device requires a path\n", PROGRAM_NAME);
                usage(stderr);
                return EXIT_FAILURE;
            }
            device = argv[index];
        } else if (strcmp(argv[index], "--help") == 0) {
            usage(stdout);
            return EXIT_SUCCESS;
        } else if (strcmp(argv[index], "--version") == 0) {
            printf("%s %s\n", PROGRAM_NAME, PROGRAM_VERSION);
            return EXIT_SUCCESS;
        } else {
            fprintf(stderr, "%s: unknown argument: %s\n",
                    PROGRAM_NAME, argv[index]);
            usage(stderr);
            return EXIT_FAILURE;
        }
    }

    if (resolve_backing_device(device, "/sys", watched_path,
                               sizeof(watched_path)) < 0) {
        fprintf(stderr, "%s: cannot resolve the single backing device for %s: %s\n",
                PROGRAM_NAME, device, strerror(errno));
        return EXIT_FAILURE;
    }

    if (check_only) {
        printf("%s\n", watched_path);
        return EXIT_SUCCESS;
    }

    if (geteuid() != 0) {
        fprintf(stderr, "%s: armed mode must run as root\n", PROGRAM_NAME);
        return EXIT_FAILURE;
    }
    if (install_signal_handlers() < 0) {
        fprintf(stderr, "%s: cannot install signal handlers: %s\n",
                PROGRAM_NAME, strerror(errno));
        return EXIT_FAILURE;
    }
    if (mlockall(MCL_CURRENT | MCL_FUTURE) < 0) {
        fprintf(stderr, "%s: cannot lock watchdog into RAM: %s\n",
                PROGRAM_NAME, strerror(errno));
        return EXIT_FAILURE;
    }

    fprintf(stderr, "%s: armed; watching %s backing %s\n",
            PROGRAM_NAME, watched_path, device);
    fflush(stderr);

    while (!stop_requested) {
        const struct timespec delay = {.tv_sec = 0, .tv_nsec = POLL_INTERVAL_NS};

        if (stat(watched_path, &watched_stat) < 0) {
            if (errno == ENOENT || errno == ENODEV) {
                fprintf(stderr, "%s: backing device disappeared; powering off now\n",
                        PROGRAM_NAME);
                fflush(stderr);
                request_poweroff();
            }

            fprintf(stderr, "%s: cannot inspect %s: %s\n",
                    PROGRAM_NAME, watched_path, strerror(errno));
            return EXIT_FAILURE;
        }

        while (nanosleep(&delay, NULL) < 0) {
            if (errno != EINTR) {
                fprintf(stderr, "%s: polling delay failed: %s\n",
                        PROGRAM_NAME, strerror(errno));
                return EXIT_FAILURE;
            }
            if (stop_requested) {
                break;
            }
        }
    }

    fprintf(stderr, "%s: stopped\n", PROGRAM_NAME);
    return EXIT_SUCCESS;
}
#endif
