#define FUSE_USE_VERSION 31

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#define _GNU_SOURCE

#ifdef linux
/* For pread()/pwrite()/utimensat() */
#define _XOPEN_SOURCE 700
#endif

#include <fuse.h>
#include <stdio.h>
#include <stdlib.h>
#include <curl/curl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <unistd.h>
#include <uuid/uuid.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#ifdef __FreeBSD__
#include <sys/socket.h>
#include <sys/un.h>
#endif
#include <sys/time.h>
#ifdef HAVE_SETXATTR
#include <sys/xattr.h>
#endif

#include "passthrough_helpers.h"

#define PORT 25555

int new_socket;
int server_fd;

void countingSeconds(int x){
    printf("\n\nTimes Up\n\n");
    send(new_socket, "1", 1, 0);
    shutdown(new_socket, 2);
    shutdown(server_fd, 2);
    close(new_socket);
    close(server_fd);
}

int server(uuid_t accessCode){
    // Creating socket file descriptor
    if((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0){
            perror("socket failed");
            exit(EXIT_FAILURE);
    }

    // Forcefully attaching socket to the port
    int opt = 1;
    if(setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))){
            perror("setsockopt");
            exit(EXIT_FAILURE);
    }

    struct sockaddr_in address;
    int addrlen = sizeof(address);
    address.sin_family = AF_INET;
    //address.sin_addr.s_addr = INADDR_ANY;
    address.sin_addr.s_addr = inet_addr("127.0.0.1");
    address.sin_port = htons(PORT);

    // Forcefully attaching socket to the port
    if(bind(server_fd, (struct sockaddr *)&address, sizeof(address))<0){
            perror("bind failed");
            exit(EXIT_FAILURE);
    }

    signal(SIGALRM, countingSeconds);
    alarm(30);

    printf("\nServer started\n\n");

    if(listen(server_fd, 3) < 0){
            perror("listen");
            exit(EXIT_FAILURE);
    }

    if((new_socket = accept(server_fd, (struct sockaddr *)&address,
                                    (socklen_t*)&addrlen))<0){
            perror("accept");
            exit(EXIT_FAILURE);
    }

    char buffer[37];

    int valread = read(new_socket , buffer, 37);

    alarm(0);

    send(new_socket, "0", 1, 0);

    if(valread == 37 && strlen(buffer) == 36){
        uuid_t new;
        char* strCode = strdup(buffer);
        if(uuid_parse(strCode, new) == 0){
            if(uuid_compare(accessCode, new) == 0){
                close(new_socket);
                close(server_fd);
                return 1;
            }
        }
        free(strCode);
    }

    close(new_socket);
    close(server_fd);
    return 0;
}

/*
"To: " to_mail "\r\n"
"From: " from_mail "\r\n",
"Subject: Access to a protected file\r\n",
"\r\n",
"You tried to access a protected file. Introduze the following code to authenticate yourself:\r\n",
"\r\n",
"CODE.\r\n",
NULL
*/
static const char *payload_text[] = {
        NULL,
        "Subject: Access to a protected file\r\n",
        "\r\n",
        "You tried to access a protected file. Introduze the following code to authenticate yourself:\r\n",
        "\r\n",
        "CODE.\r\n",
        NULL
    };

struct upload_status {
  int lines_read;
};

static size_t payload_source(void *ptr, size_t size, size_t nmemb, void *userp){
  struct upload_status *upload_ctx = (struct upload_status *)userp;
  const char *data;

  if((size == 0) || (nmemb == 0) || ((size*nmemb) < 1)) {
    return 0;
  }

  data = payload_text[upload_ctx->lines_read];

  if(data) {
    size_t len = strlen(data);
    memcpy(ptr, data, len);
    upload_ctx->lines_read++;

    return len;
  }

  return 0;
}

int sendEmail(char* from_addr, char* to_addr, uuid_t accessCode){
    char* str = malloc(9 + strlen(to_addr) + 2 + 17 + strlen(from_addr) + 2); //"To: User TO_ADDR\r\nFrom: FileSystem FROM_ADDR\r\n"
    strcpy(str, "To: User ");
    strcat(str, to_addr);
    strcat(str, "\r\nFrom: FileSystem ");
    strcat(str, from_addr);
    strcat(str, "\r\n");

    char code[37] = {};
        sprintf(code, "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
        accessCode[0], accessCode[1], accessCode[2], accessCode[3], accessCode[4], accessCode[5], accessCode[6], accessCode[7], accessCode[8], accessCode[9], accessCode[10], accessCode[11], accessCode[12], accessCode[13], accessCode[14], accessCode[15]);

    payload_text[0] = str;
    payload_text[5] = code;

    CURL *curl;
    CURLcode res = CURLE_OK;
    struct curl_slist *recipients = NULL;
    struct upload_status upload_ctx;

    upload_ctx.lines_read = 0;

    curl = curl_easy_init();

    if(curl) {
        curl_easy_setopt(curl, CURLOPT_URL, "smtp://smtp.gmail.com:587");
        curl_easy_setopt(curl, CURLOPT_USERNAME, from_addr);
        curl_easy_setopt(curl, CURLOPT_PASSWORD, password);
        curl_easy_setopt(curl, CURLOPT_MAIL_FROM, from_addr);
        curl_easy_setopt(curl, CURLOPT_USE_SSL, (long)CURLUSESSL_ALL);

        recipients = curl_slist_append(recipients, to_addr);
        curl_easy_setopt(curl, CURLOPT_MAIL_RCPT, recipients);

        curl_easy_setopt(curl, CURLOPT_READFUNCTION, payload_source);
        curl_easy_setopt(curl, CURLOPT_READDATA, &upload_ctx);
        curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
        curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);

        res = curl_easy_perform(curl);

        if(res != CURLE_OK){
            fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
        }

        curl_slist_free_all(recipients);

        curl_easy_cleanup(curl);
    }
    
    free(str);

    return (int) res;
}

char* parseEmail(char* username, char* str){
    char name[33];
    char email[1024];

    sscanf(str, "%32s %1023s", name, email);

    if(!strcmp(username, name)){
        return strdup(email);
    }

    return NULL;
}

char* getEmail(){
    char* username = strdup(getenv("USER"));

    FILE* file = fopen("/userEmails", "r");
    if(file == NULL){
        free(username);
        return NULL;
    }

    char* buffer = NULL;
    size_t bufferSize = 0;
    ssize_t nRead;
    char* email = NULL;

    while((nRead = getline(&buffer, &bufferSize, file)) != -1){
        if((email = parseEmail(username, buffer))){
            break;
        }
    }

    fclose(file);
    free(buffer);

    if(email){
        printf("Email: %s\n", email);
        return email;
    }
    
    return NULL;
}

static void *xmp_init(struct fuse_conn_info *conn, struct fuse_config *cfg){
    (void) conn;
    cfg->use_ino = 1;

    /* Pick up changes from lower filesystem right away. This is
        also necessary for better hardlink support. When the kernel
        calls the unlink() handler, it does not know the inode of
        the to-be-removed entry and can therefore not invalidate
        the cache of the associated inode - resulting in an
        incorrect st_nlink value being reported for any remaining
        hardlinks to this inode. */
    cfg->entry_timeout = 0;
    cfg->attr_timeout = 0;
    cfg->negative_timeout = 0;

    return NULL;
}

static int xmp_getattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi){
    (void) fi;
    int res;

    res = lstat(path, stbuf);
    if (res == -1)
            return -errno;

    return 0;
}

static int xmp_access(const char *path, int mask){
        int res;

        res = access(path, mask);
        if (res == -1)
                return -errno;

        return 0;
}

static int xmp_readlink(const char *path, char *buf, size_t size){
    int res;

    res = readlink(path, buf, size - 1);
    if (res == -1)
            return -errno;

    buf[res] = '\0';
    return 0;
}


static int xmp_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi, enum fuse_readdir_flags flags){
    DIR *dp;
    struct dirent *de;

    (void) offset;
    (void) fi;
    (void) flags;

    dp = opendir(path);
    if (dp == NULL)
            return -errno;

    while ((de = readdir(dp)) != NULL) {
            struct stat st;
            memset(&st, 0, sizeof(st));
            st.st_ino = de->d_ino;
            st.st_mode = de->d_type << 12;
            if (filler(buf, de->d_name, &st, 0, 0))
                    break;
    }

    closedir(dp);
    return 0;
}

static int xmp_mknod(const char *path, mode_t mode, dev_t rdev){
    int res;

    res = mknod_wrapper(AT_FDCWD, path, NULL, mode, rdev);
    if (res == -1)
            return -errno;

    return 0;
}

static int xmp_mkdir(const char *path, mode_t mode){
    int res;

    res = mkdir(path, mode);
    if (res == -1)
            return -errno;

    return 0;
}

static int xmp_unlink(const char *path){
    int res;

    res = unlink(path);
    if (res == -1)
            return -errno;

    return 0;
}

static int xmp_rmdir(const char *path){
    int res;

    res = rmdir(path);
    if (res == -1)
            return -errno;

    return 0;
}

static int xmp_symlink(const char *from, const char *to){
    int res;

    res = symlink(from, to);
    if (res == -1)
            return -errno;

    return 0;
}

static int xmp_rename(const char *from, const char *to, unsigned int flags){
    int res;

    if (flags)
            return -EINVAL;

    res = rename(from, to);
    if (res == -1)
            return -errno;

    return 0;
}

static int xmp_link(const char *from, const char *to){
    int res;

    res = link(from, to);
    if (res == -1)
            return -errno;

    return 0;
}

static int xmp_chmod(const char *path, mode_t mode, struct fuse_file_info *fi){
    (void) fi;
    int res;

    res = chmod(path, mode);
    if (res == -1)
            return -errno;

    return 0;
}

static int xmp_chown(const char *path, uid_t uid, gid_t gid, struct fuse_file_info *fi){
    (void) fi;
    int res;

    res = lchown(path, uid, gid);
    if (res == -1)
            return -errno;

    return 0;
}

static int xmp_truncate(const char *path, off_t size, struct fuse_file_info *fi){
    int res;

    if (fi != NULL)
            res = ftruncate(fi->fh, size);
    else
            res = truncate(path, size);
    if (res == -1)
            return -errno;

    return 0;
}

#ifdef HAVE_UTIMENSAT
static int xmp_utimens(const char *path, const struct timespec ts[2], struct fuse_file_info *fi){
    (void) fi;
    int res;

    /* don't use utime/utimes since they follow symlinks */
    res = utimensat(0, path, ts, AT_SYMLINK_NOFOLLOW);
    if (res == -1)
            return -errno;

    return 0;
}
#endif

static int xmp_create(const char *path, mode_t mode, struct fuse_file_info *fi){
    int res;

    res = open(path, fi->flags, mode);
    if (res == -1)
            return -errno;

    fi->fh = res;
    return 0;
}

static int xmp_open(const char *path, struct fuse_file_info *fi){
    int res;

    res = open(path, fi->flags);
    if (res == -1)
        return -errno;

    fi->fh = res;

    uuid_t accessCode;
    uuid_generate(accessCode);

    int emailSended;
    char* email = getEmail();
    if(!email){
        return -1;
    }

    for(int i=0; (emailSended = sendEmail("luisjrsm@gmail.com", email, accessCode)) != 0 && i<3; i++);

    free(email);
    
    if(emailSended != 0){
        return -1;
    }

    printf("\nEmail sent\n\n");

    if(!server(accessCode)){
        return -1;
    }

    return 0;
}

static int xmp_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi){
    int fd;
    int res;

    if(fi == NULL)
            fd = open(path, O_RDONLY);
    else
            fd = fi->fh;

    if (fd == -1)
            return -errno;

    res = pread(fd, buf, size, offset);
    if (res == -1)
            res = -errno;

    if(fi == NULL)
            close(fd);
    return res;
}

static int xmp_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi){
    int fd;
    int res;

    (void) fi;
    if(fi == NULL)
            fd = open(path, O_WRONLY);
    else
            fd = fi->fh;

    if (fd == -1)
            return -errno;

    res = pwrite(fd, buf, size, offset);
    if (res == -1)
            res = -errno;

    if(fi == NULL)
            close(fd);
    return res;
}

static int xmp_statfs(const char *path, struct statvfs *stbuf){
    int res;

    res = statvfs(path, stbuf);
    if (res == -1)
            return -errno;

    return 0;
}

static int xmp_release(const char *path, struct fuse_file_info *fi){
    (void) path;
    close(fi->fh);
    return 0;
}

static int xmp_fsync(const char *path, int isdatasync, struct fuse_file_info *fi){
    /* Just a stub.  This method is optional and can safely be left unimplemented */

    (void) path;
    (void) isdatasync;
    (void) fi;
    return 0;
}

#ifdef HAVE_POSIX_FALLOCATE
static int xmp_fallocate(const char *path, int mode, off_t offset, off_t length, struct fuse_file_info *fi){
    int fd;
    int res;

    (void) fi;

    if (mode)
            return -EOPNOTSUPP;

    if(fi == NULL)
            fd = open(path, O_WRONLY);
    else
            fd = fi->fh;

    if (fd == -1)
            return -errno;

    res = -posix_fallocate(fd, offset, length);

    if(fi == NULL)
            close(fd);
    return res;
}
#endif

#ifdef HAVE_SETXATTR
/* xattr operations are optional and can safely be left unimplemented */
static int xmp_setxattr(const char *path, const char *name, const char *value, size_t size, int flags){
    int res = lsetxattr(path, name, value, size, flags);
    if (res == -1)
            return -errno;
    return 0;
}

static int xmp_getxattr(const char *path, const char *name, char *value, size_t size){
    int res = lgetxattr(path, name, value, size);
    if (res == -1)
            return -errno;
    return res;
}

static int xmp_listxattr(const char *path, char *list, size_t size){
    int res = llistxattr(path, list, size);
    if (res == -1)
            return -errno;
    return res;
}

static int xmp_removexattr(const char *path, const char *name){
    int res = lremovexattr(path, name);
    if (res == -1)
            return -errno;
    return 0;
}
#endif /* HAVE_SETXATTR */

#ifdef HAVE_COPY_FILE_RANGE
static ssize_t xmp_copy_file_range(const char *path_in, struct fuse_file_info *fi_in, off_t offset_in, const char *path_out, struct fuse_file_info *fi_out, off_t offset_out, size_t len, int flags){
    int fd_in, fd_out;
    ssize_t res;

    if(fi_in == NULL)
            fd_in = open(path_in, O_RDONLY);
    else
            fd_in = fi_in->fh;

    if (fd_in == -1)
            return -errno;

    if(fi_out == NULL)
            fd_out = open(path_out, O_WRONLY);
    else
            fd_out = fi_out->fh;

    if (fd_out == -1) {
            close(fd_in);
            return -errno;
    }

    res = copy_file_range(fd_in, &offset_in, fd_out, &offset_out, len,
                            flags);
    if (res == -1)
            res = -errno;

    close(fd_in);
    close(fd_out);

    return res;
}
#endif

static off_t xmp_lseek(const char *path, off_t off, int whence, struct fuse_file_info *fi){
    int fd;
    off_t res;

    if (fi == NULL)
            fd = open(path, O_RDONLY);
    else
            fd = fi->fh;

    if (fd == -1)
            return -errno;

    res = lseek(fd, off, whence);
    if (res == -1)
            res = -errno;

    if (fi == NULL)
            close(fd);
    return res;
}

static struct fuse_operations xmp_oper = {
        .init           = xmp_init,
        .getattr        = xmp_getattr,
        .access         = xmp_access,
        .readlink       = xmp_readlink,
        .readdir        = xmp_readdir,
        .mknod          = xmp_mknod,
        .mkdir          = xmp_mkdir,
        .symlink        = xmp_symlink,
        .unlink         = xmp_unlink,
        .rmdir          = xmp_rmdir,
        .rename         = xmp_rename,
        .link           = xmp_link,
        .chmod          = xmp_chmod,
        .chown          = xmp_chown,
        .truncate       = xmp_truncate,
    #ifdef HAVE_UTIMENSAT
        .utimens        = xmp_utimens,
    #endif
        .open           = xmp_open,
        .create         = xmp_create,
        .read           = xmp_read,
        .write          = xmp_write,
        .statfs         = xmp_statfs,
        .release        = xmp_release,
        .fsync          = xmp_fsync,
    #ifdef HAVE_POSIX_FALLOCATE
        .fallocate      = xmp_fallocate,
    #endif
    #ifdef HAVE_SETXATTR
        .setxattr       = xmp_setxattr,
        .getxattr       = xmp_getxattr,
        .listxattr      = xmp_listxattr,
        .removexattr    = xmp_removexattr,
    #endif
    #ifdef HAVE_COPY_FILE_RANGE
        .copy_file_range = xmp_copy_file_range,
    #endif
        .lseek          = xmp_lseek,
};

int main(int argc, char* argv[]){
    //umask(0);
    return fuse_main(argc, argv, &xmp_oper, NULL);
}
