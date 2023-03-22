/*** includes ***/

#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include<ctype.h>
#include<errno.h>
#include<fcntl.h>
#include<stdarg.h>
#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<sys/ioctl.h>
#include<sys/types.h>
#include<termios.h>
#include<time.h>
#include<unistd.h>

/*** defines ***/

//CTRL_KEY macro bitwise ANDs a character with the value 00011111 in binary, this mirrors the function of a ctrl key in the terminal, stripping away bits 5 and 6 from whatever key you press in combination with ctrl
#define CTRL_KEY(k) ((k) & 0x1f)

#define EDITOR_VERSION "0.0.1"
#define TAB_STOP 8
#define QUIT_TIMES 3

enum editorKey{
    //the rest would be set to incrementing values automatically
    BACKSPACE = 127,
    ARROW_LEFT = 1000,
    ARROW_RIGHT,
    ARROW_DOWN,
    ARROW_UP,
    DEL_KEY,
    HOME_KEY,
    END_KEY,
    PAGE_UP,
    PAGE_DOWN
};

/*** data ***/

typedef struct erow{
    //struct to store a row of data

    int size;
    int rsize;
    char *chars;
    char *render;
}erow; //editor row

struct editorConfig{

    //current position of the cursor
    int cx, cy;
    int rx;
    //rowoff and coloff keeps track of the row and the column of the file the user is currently scrolled to
    int rowoff;
    int coloff;
    //for the number of rows and columns on screen
    int screenrows;
    int screencols;

    int numrows;
    erow *row;

    int dirty;
    char *filename;
    char statusmsg[80];
    time_t statusmsg_time;
    //stores the configuration of the original terminal
    struct termios orig_termios;
};

struct editorConfig e;

/*** prototypes ***/

void editorSetStatusMessage(const char *fmt, ...);
void editorRefreshScreen();
char *editorPrompt(char *prompt, void (*callback)(char *, int));

/*** terminal ***/

void die(const char *s){
    
    //clears the screen and repositions the cursor to the top left corner of the screen
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);

    //in case of errors, library functions set the global errno variable to indicate the error, perror reads the errno and prints a descriptive message
    perror(s);
    //exit the program with exit status 1(non-zero value) which indicates an error in the program
    exit(1);
}

void disableRawMode(){

    //tcsetattr resets the attributes to their original values
    //check the error and send the corresponding error code to die function which prints the error message
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &e.orig_termios) == -1)
        die("tcsetattr");
}

void enableRawMode(){

    //reads the current attributes in orig_termios
    if (tcgetattr(STDIN_FILENO, &e.orig_termios) == -1) 
        die("tcgetattr");
    //when exiting the program, disable this mode
    atexit(disableRawMode);

    struct termios raw = e.orig_termios;
    
    //modify the struct raw by changing the input, output and local flags

    //BRKINT will cause a break condition to cause a SIGINT signal to be sent to the program
    //ICRNL is used to help the terminal stop translating any carriage returns inputted by the users as new lines
    //INPCK flag enables parity checking
    //ISTRIP will change each input byte's eighth bit to 0
    //IXON flag is disabled to be able to read ctrl + s and ctrl + q which are generally used to pause or resume transmission respectively of a program
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);

    //turn off all output processing features, as it would generally translate all \n characters to \r\n
    raw.c_oflag &= ~(OPOST);

    //here ECHO is turned off, so any keystroke won't be visible on the terminal
    //turning the ICANON flag off allows us to read the input byte by byte instead of line by line 
    //turning off IEXTEN allows for ctrl + v and ctrl + o
    //turning off ISIG allows ctrl + c, ctrl + y and ctrl + z to be read by the program
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);

    //CS8 is a bit mask with mutiple bits which sets the character size to 8 bits per byte
    raw.c_cflag |= (CS8); 

    //VMIN sets the minimum number of bytes of input needed before read() can return to 0
    raw.c_cc[VMIN] = 0;
    //VTIME sets the maximum amount of time to wait before read() returns, here we set it to 1/10 of a second = 100 milliseconds
    raw.c_cc[VTIME] = 1;
    
    //pass the modified struct and write the new terminal attributes
    //TCASFLUSH function discards the remaining input before applying changes to the terminal as opposed to feeding it to the shell
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
        die("tcsetattr");
}

int editorReadKey(){
    //wait for a keypress and return it to be processed

    int nread;
    char c;
    
    //reads one byte from the standard input into character variable c
    //read returns the number of bytes that it reads
    //in case of error, error code 'read' is sent to die() function
    while ((nread = read(STDIN_FILENO, &c, 1)) != 1){
        if (nread == -1 && errno != EAGAIN)
            die("read");
    }
    //checking if c is an escape character
    if (c == '\x1b'){
        char seq[3];
        //to check if it is an escape character
        if (read(STDIN_FILENO, &seq[0], 1) != 1)
            return '\x1b';
        if (read(STDIN_FILENO, &seq[1], 1) != 1)
            return '\x1b';
        
        if (seq[0] == '['){
            if (seq[1] >= '0' && seq[1] <= '9'){
                if (read(STDIN_FILENO, &seq[2], 1) != 1)
                    return '\x1b';
                if (seq[2] == '~'){
                    switch (seq[1]){
                        case '1':
                            return HOME_KEY;
                        case '3':
                            return DEL_KEY;
                        case '4':
                            return END_KEY;
                        case '5': 
                            return PAGE_UP;
                        case '6':
                            return PAGE_DOWN;
                        case '7':
                            return HOME_KEY;
                        case '8':
                            return END_KEY;
                    }
                }
            }
            else{
                //to check which arrow key was pressed and match it with the corresponding key and return the apt value
                switch(seq[1]){
                    case 'A':
                        return ARROW_UP;
                    case 'B':
                        return ARROW_DOWN;
                    case 'C':
                        return ARROW_RIGHT;
                    case 'D':
                        return ARROW_LEFT;
                    case 'H':
                        return HOME_KEY;
                    case 'F':
                        return END_KEY;
                }
            }
        }
        else if (seq[0] == 'O'){
            switch(seq[1]){
                case 'H':
                    return HOME_KEY;
                case 'F':
                    return END_KEY;
            }
        }
        //if it does not match with any of the arrow keys, we will return the escape character
        return '\x1b';
    }
    //if it is not the escape character, we just return the character
    else{
        return c;
    }
}

int getCursorPosition(int *rows, int *columns){

    char buf[32];
    unsigned int i = 0;

    //gives the current position of the cursor
    if(write(STDOUT_FILENO, "\x1b[6n", 4) != 4) 
        return -1;

    while(i < sizeof(buf) - 1){
        if(read(STDIN_FILENO, &buf[i], 1) != 1)
            break;
        if(buf[i] == 'R')
            break;
        i++;
    }
    //end the buffer with a null character
    buf[i] = '\0';

    //skipping the first two characters during scanning
    if(buf[0] != '\x1b' || buf[1] != '[')
        return -1;
    //store the values in apt locations
    if(sscanf(&buf[2], "%d;%d", rows, columns) != 2)
        return -1;

    return 0;
}

int getWindowSize(int *rows, int *columns){
    
    struct winsize ws;
    
    //here, the function ioctl() places the number of columns and the number of rows in the given winsize struct, ws
    //in case of any errors, of if ioctl returns 0, we raise an error and return -1
    if(ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0){
        if(write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) //cursor reaches the end of the screen
            return -1;
        return getCursorPosition(rows, columns);
    }
    else{
        *columns = ws.ws_col;
        *rows = ws.ws_row;
        return 0;
    }
}

/*** row operations ***/

int editorRowCxtoRx(erow *row, int cx){
    //converts char index to render index
    int rx  = 0;
    int j;
    for(j = 0; j < cx; j++){
        if(row->chars[j] == '\t')
            rx += (TAB_STOP - 1) - (rx % TAB_STOP);
        rx++;
    }
    return rx;
}

int editorRowRxtoCx(erow *row, int rx){
    int cur_rx = 0;
    int cx;
    // looping through the chars string and calculating the current rx value
    for(cx = 0; cx < row -> size; cx++){
        if(row -> chars[cx] == '\t')
            cur_rx += (TAB_STOP - 1) - (cur_rx % TAB_STOP);
        cur_rx++;

        if(cur_rx > rx)
            return cx;
    }
    return cx;
}

void editorUpdateRow(erow *row){
    //uses the chars string of an erow to fill the contents of the render string, and each character from chars is copied to render

    int tabs = 0;
    int j;
    for(j = 0; j < row->size; j++){
        if(row->chars[j] == '\t')
            tabs++;
    }
    free(row->render);
    row->render = malloc(row->size + tabs * (TAB_STOP - 1) + 1);

    int idx = 0;
    //renders tabs as multiple space characters
    for(j = 0; j < row -> size; j++){
        if(row->chars[j] == '\t'){
            row->render[idx++] = ' ';
            while(idx % (TAB_STOP) != 0)
                row->render[idx++] = ' ';
        }
        else{
            row->render[idx++] = row->chars[j];
        }
    }
    row->render[idx] = '\0';
    row->rsize = idx;
}

void editorInsertRow(int at, char *s, size_t len){
    //allocate space for a new erow and then copy the given string to a new erow at the end of the e.row array

    if(at < 0 || at > e.numrows)
        return;

    e.row = realloc(e.row, sizeof(erow) * (e.numrows + 1));
    memmove(&e.row[at + 1], &e.row[at], sizeof(erow) * (e.numrows - at));

    e.row[at].size = len;
    e.row[at].chars = malloc(len+1);
    memcpy(e.row[at].chars, s, len);
    e.row[at].chars[len] = '\0';

    e.row[at].rsize = 0;
    e.row[at].render = NULL;
    editorUpdateRow(&e.row[at]);

    e.numrows++;
    e.dirty++;
}

void editorFreeRow(erow *row){
    free(row->render);
    free(row->chars);
}

void editorDelRow(int at){
    if(at < 0 || at > e.numrows)
        return;
    editorFreeRow(&e.row[at]);
    memmove(&e.row[at], &e.row[at + 1], sizeof(e.row) * (e.numrows - at - 1));
    e.numrows--;
    e.dirty++;
}

void editorRowInsertChar(erow *row, int at, int c){
    if(at < 0 || at > row->size)
        at = row->size;
    row->chars = realloc(row->chars, row->size + 2); //allocated 2 more bytes for the end null byte
    memmove(&row->chars[at + 1], &row->chars[at], row->size - at + 1);
    row->size++;
    row->chars[at] = c; //asign the character to its position in the array
    editorUpdateRow(row);
    e.dirty++;
}

void editorRowAppendString(erow *row, char *s, size_t len){
    row->chars = realloc(row->chars, row->size + len + 1);
    memcpy(&row->chars[row->size], s, len);
    row->size += len;
    row->chars[row->size] = '\0';
    editorUpdateRow(row);
    e.dirty++;
}

void editorRowDelChar(erow *row, int at){
    if(at < 0 || at >= row->size)
        return;
    memmove(&row->chars[at], &row->chars[at + 1], row->size - at);
    row->size--;
    editorUpdateRow(row);
    e.dirty++;
}

/*** editor operations ***/

void editorInsertChar(int c){
    if(e.cy == e.numrows){
        //if the cursor is at the last tline of the file, a new empty line is added at the end of the file
        editorInsertRow(e.numrows, "", 0);
    }
    editorRowInsertChar(&e.row[e.cy], e.cx, c);
    e.cx++;
}

void editorInsertNewLine(){
    if(e.cx == 0){
        editorInsertRow(e.cy, "", 0);
    }
    else{
        erow *row = &e.row[e.cy];
        editorInsertRow(e.cy + 1, &row->chars[e.cx], row->size - e.cx);
        row = &e.row[e.cy];
        row->size = e.cx;
        row->chars[row->size] = '\0';
        editorUpdateRow(row);
    }
    e.cy++;
    e.cx = 0;
}

void editorDelChar(){
    if(e.cy == e.numrows)
        return;
    if(e.cx == 0 && e.cy == 0)
        return;
    
    erow *row = &e.row[e.cy];
    if(e.cx > 0){
        editorRowDelChar(row, e.cx - 1);
        e.cx--;
    }
    else{
        e.cx = e.row[e.cy - 1].size;
        editorRowAppendString(&e.row[e.cy - 1], row->chars, row->size);
        editorDelRow(e.cy);
        e.cy--;
    }
}

/*** file i/o ***/

char *editorRowsToString(int *buflen){
    int totlen = 0; 
    int j;
    for(j = 0; j < e.numrows; j++)
        totlen += e.row[j].size + 1;
    *buflen = totlen;

    char *buf = malloc(totlen);
    char *p = buf;
    for(j = 0; j < e.numrows; j++){
        memcpy(p, e.row[j].chars, e.row[j].size);
        p += e.row[j].size;
        *p = '\n';
        p++;
    }
    return buf;
}

void editorOpen(char *filename){

    free(e.filename);
    e.filename = strdup(filename);

    //opens the file passed as an argument
    FILE *fp = fopen(filename, "r");
    if(!fp)
        die("fopen");

    char *line = NULL;
    size_t linecap = 0;
    ssize_t linelen;

    //reads a line and processes it in line
    //linecap stores the size of the memory allocated
    while((linelen = getline(&line, &linecap, fp)) != -1){
        while(linelen > 0 && (line[linelen - 1] == '\n' || line[linelen - 1] == '\r'))
            linelen --;
        editorInsertRow(e.numrows, line, linelen);
    }
    free(line);
    fclose(fp);
    e.dirty = 0;
}

void editorSave(){
    if (e.filename == NULL){
        e.filename = editorPrompt("Save as: %s (ESC to cancel)", NULL);
        if (e.filename == NULL){
            editorSetStatusMessage("Save aborted");
            return;
        }
    }

    int len; 
    char *buf = editorRowsToString(&len);

    int fd = open(e.filename, O_RDWR | O_CREAT, 0644);
    if (fd != -1){
        if (ftruncate(fd, len) != -1){
        //in case of no error by ftruncate
            if (write(fd, buf, len) == len){
                //if the write operation is successful
                close(fd);
                free(buf);
                e.dirty = 0;
                editorSetStatusMessage("%d bytes written to disk", len);
                return;
            }
        }
        close(fd);
    }
    free(buf);
    editorSetStatusMessage("Can not save the file due to I/O error: %s", strerror(errno));
}

/*** find ***/

void editorFindCallback(char *query, int key){
    static int last_match = -1;
    static int direction = 1;

    if (key == '\r' || key == '\x1b'){
        last_match = -1;
        direction = 1;
        return;
    }
    else if (key == ARROW_RIGHT || key == ARROW_DOWN){
        direction = 1;
    }
    else if (key == ARROW_LEFT || key == ARROW_UP){
        direction = -1;
    }
    else{
        last_match = -1; 
        direction = 1;
    }

    if (last_match == -1)
        direction = 1;
    int current = last_match;
    int i;

    // looping through all the rows of the file and using strstr() to check if the query is the substring of the current row
    for(i = 0; i < e.numrows; i++){
        current += direction;
        // allows the user to go from the end of the file to the beginning of the file
        if (current == -1)
            current = e.numrows - 1;
        else if (current == e.numrows)
            current = 0;
        erow *row = &e.row[current];

        char *match = strstr(row -> render, query);
        if(match){
            last_match = current;
            e.cy = current;;
            e.cx = editorRowRxtoCx(row, match - row -> render);
            e.rowoff = e.numrows;
            break;
        }
    }
}

void editorFind(){
    int saved_cx = e.cx;
    int saved_cy = e.cy;
    int saved_rowoff = e.rowoff;
    int saved_coloff = e.coloff;

    char *query = editorPrompt("Search: %s (Use ESC / Arrows / Enter)", editorFindCallback);
    if (query)
        free(query);
    else{
        e.cx = saved_cx;
        e.cy = saved_cy;
        e.rowoff = saved_rowoff;
        e.coloff = saved_coloff;
    }
}

/*** append buffer ***/

struct abuf{
    char *b;
    int len;
};

//represents an empty buffer 
#define ABUF_INIT {NULL, 0}

void abAppend(struct abuf *ab, const char *s, int len){
    
    //requesting for sufficient memory
    char *new = realloc(ab->b, ab->len+len);

    if(new == NULL) return;
    //to copy the string s at the end of the current buffer
    memcpy(&new[ab->len], s, len);
    ab->b = new;
    ab->len += len;
}

void abFree(struct abuf *ab){
    //deallocates the dynamic memory used by abuf
    free(ab->b);
}

/*** output ***/

void editorScroll(){
    //if the cursor has moved outside of visible window, we adjust e.rowoff value such that the cursor is in the visible window
    e.rx = 0;

    if(e.cy < e.numrows){
        //set rx to proper value
        e.rx = editorRowCxtoRx(&e.row[e.cy], e.cx);
    }

    if (e.cy < e.rowoff){ //checks if the cursor is above the visible window, if so, scroll to where the cursor is
        e.rowoff = e.cy;
    }
    if(e.cy >= e.rowoff + e.screenrows){ //
        e.rowoff = e.cy - e.screenrows + 1;
    }
    if(e.rx < e.coloff){
        e.coloff = e.rx;
    }
    if(e.rx >= e.coloff + e.screencols){
        e.coloff = e.rx - e.screencols + 1;
    }
}

void editorDrawRows(struct abuf *ab){
    //to draw a column of tildes on the left side
    
    int y;
    //draws tildes for each row, which is the number of rows on the screen
    for(y = 0; y < e.screenrows ; y++){
        //to get the row of the file to be displayed at each position, e.rowoff is added to the y value 
        int filerow = y + e.rowoff;
        if(filerow >= e.numrows){
        //checks whether the row currently being drawn is part of the text buffer or a row that comes after the end of the text buffer
            //welcome message is only presented if no file is provided as an argument while opening
            if(e.numrows == 0 && y == e.screenrows / 3){
                char welcome[80];
                //printing the editor version as a welcome message for the user
                int welcomelen = snprintf(welcome, sizeof(welcome), "Editor Version %s", EDITOR_VERSION);
                if(welcomelen > e.screencols)
                    welcomelen = e.screencols;

                //to centre the editor version message, padding is added
                int padding = (e.screencols - welcomelen) / 2;
                if(padding){
                    abAppend(ab, "~", 1);
                    padding--;
                }
                while(padding--){
                    abAppend(ab, " ", 1);
                }
                abAppend(ab, welcome, welcomelen);
            }
            else{
                abAppend(ab, "~", 1);
            }
        }
        else{
            int len = e.row[filerow].rsize - e.coloff;
            if(len < 0)
                len = 0;
            if(len > e.screencols)
                len = e.screencols;
                //truncate the line if it is larger than what the screen can fit
            abAppend(ab, &e.row[filerow].render[e.coloff], len);
        }
        //the K command erases the current line to the right of the cursor by deafult 0 value
        abAppend(ab, "\x1b[K", 3);
        //the screen will scroll down by one unit each time to make room for the newline which shows the information of the file
        abAppend(ab, "\r\n", 2);
    }
}

void editorDrawStatusBar(struct abuf *ab){
    //appending a line at the line with inverted color scheme, to show the file name and number of lines, etc
    abAppend(ab, "\x1b[7m", 4);
    
    char status[80], rstatus[80];

    int len = snprintf(status, sizeof(status), "%.20s - %d lines %s", e.filename ? e.filename : "[No Name]", e.numrows, e.dirty ? "modified" : "");
    int rlen = snprintf(rstatus, sizeof(rstatus), "%d/%d", e.cy + 1, e.numrows); //prints the current line the cursor is on and the total numer of lines
    if(len > e.screencols)
        len = e.screencols;
    abAppend(ab, status, len);

    while(len < e.screencols){
        if(e.screencols - len == rlen){
            abAppend(ab, rstatus, rlen);
            break;
        }
        else{
            abAppend(ab, " ", 1);
            len++;
        }
    }
    abAppend(ab, "\x1b[m", 3);
    abAppend(ab, "\r\n", 2);
}

void editorDrawMessageBar(struct abuf *ab){
    abAppend(ab, "\x1b[K", 3);
    int msglen = strlen(e.statusmsg);
    if(msglen > e.screencols)
        msglen = e.screencols;
    if(msglen && time(NULL) - e.statusmsg_time < 5)
        abAppend(ab, e.statusmsg, msglen);
}

void editorRefreshScreen(){

    editorScroll();

    struct abuf ab = ABUF_INIT;

    //hide the cursor when terminal is drawing to the screen
    abAppend(&ab, "\x1b[?25l", 6);
    //H command is for the cursor position, here we are taking the default values 1, 1 and repositioning it to the top left corner of the screen
    abAppend(&ab, "\x1b[H", 3);

    //draws a column of tildes on the left side of the screen
    editorDrawRows(&ab);
    editorDrawStatusBar(&ab);
    editorDrawMessageBar(&ab);

    //moves the cursor to position stored in e.cx and e.cy, 1 is added to convert 0 indexed values to values with index 1
    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", e.cy - e.rowoff + 1, e.rx - e.coloff + 1);
    abAppend(&ab, buf, strlen(buf));

    //show the cursor before the screen refreshes
    abAppend(&ab, "\x1b[?25h", 6);

    write(STDOUT_FILENO, ab.b, ab.len);
    abFree(&ab);
}

void editorSetStatusMessage(const char *fmt, ...){
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(e.statusmsg, sizeof(e.statusmsg), fmt, ap);
    va_end(ap);
    e.statusmsg_time = time(NULL);
}

/*** input ***/

char *editorPrompt(char *prompt, void (*callback)(char *, int)){
    size_t bufsize = 128;
    char *buf = malloc(bufsize);

    size_t buflen = 0;
    buf[0] = '\0';

    while(1){
        editorSetStatusMessage(prompt, buf);
        editorRefreshScreen();

        int c = editorReadKey();
        if (c == DEL_KEY || c == CTRL_KEY('h') || c == BACKSPACE){
            if (buflen != 0)
                buf[--buflen] = '\0';
        }
        else if (c == '\x1b'){
            editorSetStatusMessage("");
            if (callback)
                callback(buf, c);
            free(buf);
            return NULL;
        }
        else if (c == '\r'){
            if (buflen != 0){
                editorSetStatusMessage("");
                if (callback)
                    callback(buf, c);
                return buf;
            }
        }
        else if (!iscntrl(c) && c < 128){
            if (buflen == bufsize - 1){
                bufsize *= 2;
                buf = realloc(buf, bufsize);
            }
            buf[buflen++] = c;
            buf[buflen] = '\0';
        }
        if(callback)
            callback(buf, c);
    }
}

void editorMoveCursor(int key){
    //updating the e.cx and e.cy values while checking the constraints that the cursor doesn't go out of bounds of the screen

    //since e.cy is allowed to be one past the last line of the file, the ternary operation is used to check if the cursor is on an actual line
    erow *row = (e.cy >= e.numrows) ? NULL : &e.row[e.cy];
    switch(key){
        case ARROW_LEFT:
            if (e.cx != 0)
                e.cx--;
            else if (e.cy > 0){
                //pressing the left arrow key at the beginning of the line takes the cursor to the end of the previous line
                e.cy--;
                e.cx = e.row[e.cy].size;
            }
            break;
        case ARROW_DOWN:
            if (e.cy < e.numrows)
                e.cy++;
            break;
        case ARROW_RIGHT:
            if (row && e.cx < row->size)
                e.cx++;
            else if (row && e.cx == row->size){
                //pressing the right arrow key at the end of a line takes the cursor to the beginning of the next line
                e.cy++;
                e.cx = 0;
            }
            break;
        case ARROW_UP:
            if (e.cy != 0)
                e.cy--;
            break;
    }

    row = (e.cy >= e.numrows) ? NULL : &e.row[e.cy];
    int rowlen = row ? row->size : 0;
    if (e.cx > rowlen){
        //we set e.cx to the end of the line if e.cx is to the right of the end of that line
        e.cx = rowlen;
    }
}

void editorProcessKeypress(){
    //gets a keyPress and processes it 

    static int quit_times = QUIT_TIMES;

    int c = editorReadKey();

    switch(c){

        case '\r':
            editorInsertNewLine();
            break;
        
        //if it is ctrl + q, then exit from the terminal
        case CTRL_KEY('q'):
            //clears the screen and repositions the cursor to the top left corner of the screen
            if(e.dirty && quit_times > 0){
                editorSetStatusMessage("Warning! File has unsaved changes. Press ctrl-q %d more times to quit. ", quit_times);
                quit_times--;
                return;
            }
            write(STDOUT_FILENO, "\x1b[2J", 4);
            write(STDOUT_FILENO, "\x1b[H", 3);
            exit(0);
            break;
        
        case CTRL_KEY('s'):
            editorSave();
            break;

        case CTRL_KEY('f'):
            editorFind();
            break;

        case HOME_KEY:
            e.cx = 0;
            break;
        case END_KEY:
        //brings the cursor at the end of the current line
            if (e.cy < e.numrows)
                e.cx = e.row[e.cy].size;
            break;

        case BACKSPACE:
        case CTRL_KEY('h'):
        case DEL_KEY:
            //deleting is the same as using the right arrow and backspace
            if(c == DEL_KEY)
                editorMoveCursor(ARROW_RIGHT);
            editorDelChar();
            break;

        case PAGE_UP:
        case PAGE_DOWN:
        {
            if(c == PAGE_UP){
                e.cy = e.rowoff;
            }
            else if(c == PAGE_DOWN){
                e.cy = e.rowoff + e.screenrows - 1;
                if(e.cy > e.numrows)
                    e.cy = e.numrows;
            }
            int times = e.screenrows;
            while (times--)
                editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
        }
        break;

        case ARROW_LEFT:
        case ARROW_DOWN:
        case ARROW_RIGHT:
        case ARROW_UP:
            editorMoveCursor(c);
            break;

        case CTRL_KEY('l'):
        case '\x1b':
            break;

        default:
        //if no special key, insert it directly in the text editor
            editorInsertChar(c);
            break;
    }
    quit_times = QUIT_TIMES;
}

/*** init ***/

void initEditor(){
    //initialize the cursor values, such that it is at the top left corner of the screen
    e.cx = 0;
    e.cy = 0;
    e.rx = 0;
    //initialized to 0, which means that it'll be scrolled to the top left by default
    e.rowoff = 0; 
    e.coloff = 0;
    e.numrows = 0;
    e.row = NULL;
    e.dirty = 0;
    e.filename = NULL;
    e.statusmsg[0] = '\0';
    e.statusmsg_time = 0;
    //we get the window size and store them successfully in editorConfig e
    if(getWindowSize(&e.screenrows, &e.screencols) == -1)
        die("getWindowSize");
    e.screenrows -= 2; //the last two lines shouldn't be scrolled
}

int main(int argc, char *argv[]){

    enableRawMode();
    initEditor();
    if(argc >= 2){
        editorOpen(argv[1]);
    }

    editorSetStatusMessage("HELP: Ctrl - S = save | Ctrl - Q = quit | Ctrl - F = find");

    while(1){
        editorRefreshScreen();
        editorProcessKeypress();
    }
    return 0;
}
