# FileSystem-with-Email

A filesystem that sends an email with a code to the user, when the user wants to read a file using `cat`, `more`, etc.

The users email must be in the root directory, with the name equal to "userEmails" and have the following format:
- Each line represents a user;
- Each line consists in two words, user name and email

After receiving the code, the user must execute the "InsertCode.c" code and introduce the received code.
