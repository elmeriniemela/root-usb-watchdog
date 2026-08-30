#define _GNU_SOURCE
#define ROOT_USB_WATCHDOG_NO_MAIN
#include "../root-usb-watchdog.c"

#include <assert.h>
#include <fcntl.h>
#include <sys/types.h>

static void make_directory(const char *path)
{
    if (mkdir(path, 0700) < 0) {
        perror(path);
        exit(EXIT_FAILURE);
    }
}

static void prepare_tree(char *root, size_t root_size)
{
    char template[] = "/tmp/root-usb-watchdog-test.XXXXXX";
    char path[PATH_MAX];
    char *created = mkdtemp(template);

    assert(created != NULL);
    assert(strlen(created) + 1 <= root_size);
    strcpy(root, created);

    snprintf(path, sizeof(path), "%s/dev", root);
    make_directory(path);
    snprintf(path, sizeof(path), "%s/dev/block", root);
    make_directory(path);
    snprintf(path, sizeof(path), "%s/dev/block/253:0", root);
    make_directory(path);
    snprintf(path, sizeof(path), "%s/dev/block/253:0/slaves", root);
    make_directory(path);
    snprintf(path, sizeof(path), "%s/devices", root);
    make_directory(path);
}

static void remove_tree(const char *root)
{
    static const char *const relative_paths[] = {
        "/dev/block/253:0/slaves/sdb3",
        "/dev/block/253:0/slaves/sdc3",
        "/devices/sdb3",
        "/devices/sdc3",
        "/dev/block/253:0/slaves",
        "/dev/block/253:0",
        "/dev/block",
        "/dev",
        "/devices",
        "",
    };
    char path[PATH_MAX];
    size_t index;

    for (index = 0; index < sizeof(relative_paths) / sizeof(relative_paths[0]);
         ++index) {
        assert(snprintf(path, sizeof(path), "%s%s", root,
                        relative_paths[index]) < (int)sizeof(path));
        if (unlink(path) < 0 && errno != ENOENT && errno != EISDIR) {
            perror(path);
            exit(EXIT_FAILURE);
        }
        if (rmdir(path) < 0 && errno != ENOENT && errno != ENOTDIR) {
            perror(path);
            exit(EXIT_FAILURE);
        }
    }
}

static void test_no_slave(void)
{
    char root[128];
    char resolved[PATH_MAX];

    prepare_tree(root, sizeof(root));
    errno = 0;
    assert(resolve_slave_from_numbers(root, 253, 0, resolved,
                                      sizeof(resolved)) == -1);
    assert(errno == ENODEV);
    remove_tree(root);
}

static void add_slave(const char *root, const char *name)
{
    char target[PATH_MAX];
    char link_path[PATH_MAX];

    snprintf(target, sizeof(target), "%s/devices/%s", root, name);
    make_directory(target);
    snprintf(link_path, sizeof(link_path), "%s/dev/block/253:0/slaves/%s",
             root, name);
    if (symlink(target, link_path) < 0) {
        perror("symlink");
        exit(EXIT_FAILURE);
    }
}

static void test_one_slave(void)
{
    char root[128];
    char expected[PATH_MAX];
    char resolved[PATH_MAX];

    prepare_tree(root, sizeof(root));
    add_slave(root, "sdb3");
    snprintf(expected, sizeof(expected), "%s/devices/sdb3", root);

    assert(resolve_slave_from_numbers(root, 253, 0, resolved,
                                      sizeof(resolved)) == 0);
    assert(strcmp(resolved, expected) == 0);
    remove_tree(root);
}

static void test_multiple_slaves(void)
{
    char root[128];
    char resolved[PATH_MAX];

    prepare_tree(root, sizeof(root));
    add_slave(root, "sdb3");
    add_slave(root, "sdc3");

    errno = 0;
    assert(resolve_slave_from_numbers(root, 253, 0, resolved,
                                      sizeof(resolved)) == -1);
    assert(errno == E2BIG);
    remove_tree(root);
}

int main(void)
{
    test_no_slave();
    test_one_slave();
    test_multiple_slaves();
    puts("resolver tests passed");
    return EXIT_SUCCESS;
}
