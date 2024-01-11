struct fuse_operations {
	int (*open) (const char *);
	int (*read) (int fd, int r, void *buf);
	int (*write) (int fd, int r, void *buf);
	int (*close) (int fd);
	int (*mkdir) (const char *);
	int (*opendir) (const char *);
};

	
