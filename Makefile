CFLAGS=-Wall `pkg-config fuse3 --cflags --libs` -lcurl -luuid

compile:
	gcc SSI.c -o SSI $(CFLAGS)
	gcc InsertCode.c -o InsertCode

clean:
	rm -f SSI
	rm -f InsertCode

mkdir:
	mkdir -p Mount

rmdir:
	rm -rf Mount

unmount:
	fusermount -u -q Mount
