#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <termios.h>
#include <unistd.h>

static int failures, verify;
static void row(const char *kind, int fd, int expected)
{
    struct termios t = {0};
    struct winsize w = {0};
    int value, e;
#define QUERY(name, expr, good) do { \
    errno = 0; value = (expr); e = errno; \
    printf("%s %s value=%d errno=%d\n", kind, name, value, e); \
    if (verify && (value != (expected ? (good ? 0 : -1) : (good ? 1 : 0)) || e != expected)) failures++; \
} while (0)
    QUERY("isatty", isatty(fd), 1);
    QUERY("tcgetattr", tcgetattr(fd, &t), 0);
    QUERY("tcsetattr", tcsetattr(fd, TCSANOW, &t), 0);
    QUERY("winsize", ioctl(fd, TIOCGWINSZ, &w), 0);
    errno = 0;
    char *name = ttyname(fd);
    e = errno;
    printf("%s ttyname value=%s errno=%d\n", kind, name ? name : "NULL", e);
    if (verify && ((name != NULL) != (expected == 0) || e != expected)) failures++;
#undef QUERY
}
int main(int argc, char **argv)
{
    verify = argc > 1 && !strcmp(argv[1], "verify");
    int f = open("/tmp/tty-errno-file", O_CREAT|O_RDWR, 0600);
    int p[2];
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (f < 0 || pipe(p) || sock < 0) { perror("descriptor setup"); return 2; }
    row("file", f, ENOTTY);
    row("pipe", p[0], ENOTTY);
    int nullfd = open("/dev/null", O_RDWR);
    row("null", nullfd, ENOTTY);
    row("socket", sock, ENOTTY);
    close(f);
    row("closed", f, EBADF);
    if (argc > 2 && !strcmp(argv[2], "terminal")) {
        row("terminal", 0, 0);
        row("inherited-fd5", 5, 0);
        int duplicated = fcntl(0, F_DUPFD, 10);
        if (duplicated < 0) { perror("dup terminal"); return 2; }
        row("duplicated", duplicated, 0);
        int fresh = open("/dev/tty", O_RDWR);
        if (fresh < 0) { perror("open /dev/tty"); return 2; }
        row("fresh-tty", fresh, 0);
        close(duplicated); close(fresh);
    }
    close(p[0]); close(p[1]); close(nullfd); close(sock);
    return failures != 0;
}
