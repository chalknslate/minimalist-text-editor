#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <termios.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
static struct termios saved;
static struct termios current;
#define EXQUIT 0
#define EDMODE 1
#define FCHANG 2
//editable text buffer
struct tbuf {
  char* t;
  int len;
};

//appendable buffer, for quick terminal updates.
struct wbuf {
  char* buf;
  //cursor position
  int crp;
  int size;
};
//reset wbuf
void rwbuf(struct wbuf* wb) {
  wb->crp=0;
  return;
}

int cwidth(char c, int col) {
  if (c == '\t') {
    return 4 - (col % 4);
  }

  return 1;
}
//append to wbuf
int apwbuf(struct wbuf* wb, const char* s) {
  if(wb->crp+strlen(s)>wb->size) {
    return -1;
  }
  memcpy(wb->buf+wb->crp, s, strlen(s));
  wb->crp+=strlen(s);
  return 0;
}
struct FInfo {
  FILE* fp;
  char* path;
  struct wbuf wbuf;
  struct tbuf tbuf;
  //cursor x
  int cx;
  //cursor y
  int cy;
  //width of terminal aka columns
  int t_w;
  //height of terminal aka rows
  int t_h;
  int rowoff;
  int coloff;
  //flags, such as mode, file changed, and quit
  //indexed by defines
  int fl[4];
};

void load_tbuf(struct FInfo* f) {
  fseek(f->fp, 0, SEEK_END);
  f->tbuf.len = ftell(f->fp);
  fseek(f->fp, 0, SEEK_SET);
  f->tbuf.t = malloc(sizeof(char)*f->tbuf.len);
  fread(f->tbuf.t, sizeof(char), f->tbuf.len, f->fp);
  return;
}
void get_win_size(struct FInfo* f) {
  struct winsize w;
  ioctl(0, TIOCGWINSZ, &w);

  f->t_w = w.ws_col;
  f->t_h = w.ws_row;
}
void save_term() {
  tcgetattr(STDIN_FILENO, &saved);
  return;
}
int line_cursor_x(struct FInfo *f, int target_y, int target_x) {
  int x = 1;
  int y = 1;
  int pos = 0;

  while (pos < f->tbuf.len) {
    if (y == target_y) {
      if (x >= target_x) {
        return x;
      }

      if (f->tbuf.t[pos] == '\t') {
        int width = cwidth('\t', x - 1);
        if (target_x < x + width) {
          return x;
        }
      }
    }

    if (f->tbuf.t[pos] == '\n') {
      if (y == target_y) {
        return x;
      }

      y++;
      x = 1;
    } else {
      x += cwidth(f->tbuf.t[pos], x - 1);
    }

    pos++;
  }

  if (y == target_y) {
    return x;
  }

  return -1;
}
int ctopos(struct FInfo *f) {
  int x = 1;
  int y = 1;
  int pos = 0;

  while (pos < f->tbuf.len) {
    if (x == f->cx && y == f->cy) {
      return pos;
    }

    if (f->tbuf.t[pos] == '\n') {
      y++;
      x = 1;
    } else {
      x += cwidth(f->tbuf.t[pos], x - 1);
    }

    pos++;
  }

  return pos;
}
char deletec(struct FInfo* f, char c) {
  int pos = ctopos(f);

  if (pos == 0) {
    return 0;
  }

  pos--;

  char del = f->tbuf.t[pos];

  memmove(
          f->tbuf.t + pos,
          f->tbuf.t + pos + 1,
          f->tbuf.len - pos - 1
          );

  f->tbuf.len--;

  if (f->tbuf.len == 0) {
    free(f->tbuf.t);
    f->tbuf.t = NULL;
  } else {
    char *tmp = realloc(f->tbuf.t, f->tbuf.len);

    if (tmp != NULL) {
      f->tbuf.t = tmp;
    }
  }

  return del;
}
void insertc(struct FInfo* f, char c) {
  int pos = ctopos(f);
  f->tbuf.t = realloc(f->tbuf.t, f->tbuf.len+1);
  memmove(f->tbuf.t+pos+1, f->tbuf.t+pos, f->tbuf.len-pos);
  f->tbuf.t[pos] = c;
  f->tbuf.len++;
}


void clean_tbuf(struct FInfo* f) {
  memset(f->tbuf.t, 0, f->tbuf.len);
  write(STDOUT_FILENO, "\x1b[2J", 4);
  write(STDOUT_FILENO, "\x1b[H", 3);
  f->tbuf.len = 0;
}
void setup_term() {
  current = saved;
  current.c_iflag &= ~(ISTRIP | INPCK | IXON | ICRNL | BRKINT);

  current.c_oflag &= ~(OPOST);

  current.c_cflag |= (CS8);
  
  current.c_lflag &= ~(ICANON | ECHO | ISIG | IEXTEN);

  current.c_cc[VMIN] = 1;
  current.c_cc[VTIME] = 0;
  tcsetattr(STDIN_FILENO, TCSANOW, &current);
  write(STDOUT_FILENO, "\x1b[2J", 4);
  write(STDOUT_FILENO, "\x1b[H", 3);
  return;
}
void reset_term() {
  write(STDOUT_FILENO, "\x1b[2J", 4);
  write(STDOUT_FILENO, "\x1b[H", 3);
  tcsetattr(STDIN_FILENO, TCSANOW, &saved);
  return;
}
void clear_term(struct FInfo* f) {
  if(f->fl[FCHANG] == 0) {
    //return;
  }
  write(STDOUT_FILENO, "\x1b[2J", 4);
  write(STDOUT_FILENO, "\x1b[H", 3);
  return;
}
void mvcursor(struct FInfo *f, int x, int y) {
  char buf[32];
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", y, x);
  write(STDOUT_FILENO, buf, strlen(buf));
}
int pbcursor(struct FInfo *f) {
  int x = 1;
  int y = 1;
  int pos = 0;

  while (pos < f->tbuf.len) {
    if (x == f->cx && y == f->cy) {
      return pos;
    }

    if (f->tbuf.t[pos] == '\n') {
      y++;
      x = 1;
    } else {
      x += cwidth(f->tbuf.t[pos], x - 1);
    }

    pos++;
  }

  return pos;
}
void ptcursor(struct FInfo *f, int target, int *x, int *y) {
  *x = 1;
  *y = 1;

  for (int i = 0; i < target; i++) {
    if (f->tbuf.t[i] == '\n') {
      (*y)++;
      *x = 1;
    } else {
      *x += cwidth(f->tbuf.t[i], *x - 1);
    }
  }
}
void scroll(struct FInfo *f) {
  if (f->cy - 1 < f->rowoff) {
    f->rowoff = f->cy - 1;
    f->fl[FCHANG]=1;
  }

  if (f->cy - 1 >= f->rowoff + f->t_h) {
    f->rowoff = f->cy - f->t_h;
    f->fl[FCHANG]=1;
  }

  if (f->cx - 1 < f->coloff) {
    f->coloff = f->cx - 1;
    f->fl[FCHANG]=1;
  }

  if (f->cx - 1 >= f->coloff + f->t_w) {
    f->coloff = f->cx - f->t_w;
    f->fl[FCHANG]=1;
  }
}
int read_char(struct FInfo* f) {
  int nread;
  char c;
  while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
    
  }
  if (c == '\x1b') {
    char seq[3];
    if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
    if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';
    if (seq[0] == '[') {
      if (seq[1] >= '0' && seq[1] <= '9') {
        if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
        if (seq[2] == '~') {
          switch (seq[1]) {
            case '5': return 2000; // PAGE_UP
            case '6': return 2001; // PAGE_DOWN
          }
        }
      } else {
        switch (seq[1]) {
          case 'A': return 1001; // ARROW_UP
          case 'B': return 1002; // ARROW_DOWN
          case 'C': return 1003; // ARROW_RIGHT
          case 'D': return 1004; // ARROW_LEFT
        }
      }
    }
    return '\x1b';
  } else {
    return c;
  }
}
void handle_char(struct FInfo * f, int c) {
  switch(c) {
    //esc in ascii
    case 27: {
      f->fl[EDMODE] = !(f->fl[EDMODE]);
      break;
    }
      //ctrl q in ascii, all letters follow this formula: (x) & 0x1F)
    case 17: {
      f->fl[EXQUIT] = 1;
      break;
    }
    case ('e' & 0x1F): {
      clean_tbuf(f);
      f->fl[FCHANG] = 1;
      break;
    }
    case ('s' & 0x1F): {
      
      freopen(f->path, "w", f->fp);
      fseek(f->fp, 0, SEEK_SET);
      fwrite(f->tbuf.t, sizeof(char), f->tbuf.len, f->fp);
      break;
    }
    case 2000: {
      write(STDOUT_FILENO, "\x1b[5~", 4);
      break;
    }
    case 2001: {
      write(STDOUT_FILENO, "\x1b[6~", 4);
      break;
    }
    case 1001: {
      if (f->cy > 1) {
        int target_y = f->cy - 1;
        int target_x = f->cx;

        int new_x = line_cursor_x(f, target_y, target_x);

        if (new_x != -1) {
          f->cy = target_y;
          f->cx = new_x;
        }
      }

      break;
    }

    case 1002: { 
      int target_y = f->cy + 1;
      int target_x = f->cx;

      int new_x = line_cursor_x(f, target_y, target_x);

      if (new_x != -1) {
        f->cy = target_y;
        f->cx = new_x;
      }

      break;
    }
    case 1003: {
      int pos = ctopos(f);

      if (pos < f->tbuf.len) {
        char c = f->tbuf.t[pos];

        if (c == '\n') {
          break;
        }

        f->cx += cwidth(c, f->cx - 1);
      }

      break;
    }
    case 1004: {
      int pos = ctopos(f);
      if (pos == 0) {
        break;
      }
      pos--;
      char c = f->tbuf.t[pos];
      if (c == '\n') {
        f->cy--;
        f->cx = 1;
        int p = ctopos(f);
        while (p < f->tbuf.len && f->tbuf.t[p] != '\n') {
          f->cx += cwidth(f->tbuf.t[p], f->cx - 1);
          p++;
        }
      } else {
        int x = 1;
        int y = 1;
        for (int i = 0; i < pos; i++) {
          if (f->tbuf.t[i] == '\n') {
            y++;
            x = 1;
          } else {
            x += cwidth(f->tbuf.t[i], x - 1);
          }
        }
        f->cx = x;
        f->cy = y;
      }
      break;
    }
    case 127:
    case '\b': {
      int pos = ctopos(f);

      if (pos == 0) {
        break;
      }

      pos--;

      char del = f->tbuf.t[pos];

      if (del == '\t') {
        int tab_col = 0;
        for (int i = 0; i < pos; i++) {
          if (f->tbuf.t[i] == '\n') {
            tab_col = 0;
          } else {
            tab_col += cwidth(f->tbuf.t[i], tab_col);
          }
        }

        int width = cwidth('\t', tab_col);

        deletec(f, c);
        f->cx -= width;

        if (f->cx < 1) {
          f->cx = 1;
        }

      } else if (del == '\n') {
        deletec(f, c);

        f->cy--;
        f->cx = 1;

        int p = ctopos(f);

        while (p < f->tbuf.len && f->tbuf.t[p] != '\n') {
          f->cx += cwidth(f->tbuf.t[p], f->cx - 1);
          p++;
        }

      } else {
        deletec(f, c);

        f->cx--;

        if (f->cx < 1) {
          f->cx = 1;
        }
      }
      f->fl[FCHANG] = 1;
      break;
    }
    case '\t': {
      int width = cwidth('\t', f->cx - 1);

      insertc(f, '\t');
      f->cx += width;
      f->fl[FCHANG] = 1;
      break;
    }
    case '\r': {
      insertc(f, '\n');
      f->cy++;
      f->cx = 1;
      f->fl[FCHANG] = 1;
      break;
    }
    default: {
      insertc(f,c);
      f->cx++;
      f->fl[FCHANG] = 1;
      break;
    }
  }
}
int handle_file_error(int e) {
  switch(e) {
    case ENOENT: {
      printf("File does not exist, and cannot be created..\n");
      return -1;
      break;
    }
    case EACCES: {
      printf("Permission denied.\n");
      break;
    }
  }
}
void write_to_wbuf(struct FInfo* f) {
  rwbuf(&(f->wbuf));
  fseek(f->fp, 0, SEEK_SET);
  char buf[1024];
  while(fgets(buf, sizeof(char)*f->t_w, f->fp)!=NULL) {
    apwbuf(&(f->wbuf), buf);
  }
}
void write_to_wbuf_ft(struct FInfo *f) {
  rwbuf(&f->wbuf);

  int row = 0;
  int col = 0;

  for (int i = 0; i < f->tbuf.len; i++) {
    
    if (f->tbuf.t[i] == '\n') {
      if (row >= f->rowoff && row < f->rowoff + f->t_h -1) {
        apwbuf(&f->wbuf, "\r\n");
      }
      
      row++;
      col = 0;
      continue;
    }

    if (f->tbuf.t[i] == '\t') {
      int spaces = 4 - (col % 4);

      if (row >= f->rowoff && row < f->rowoff + f->t_h -1) {
        for (int j = 0; j < spaces; j++) {
          apwbuf(&f->wbuf, " ");
        }
      }

      col += spaces;
      continue;
    }
    
    if (row >= f->rowoff && row < f->rowoff + f->t_h - 1 &&col >= f->coloff && col < f->coloff + f->t_w - 1) {
      char c[2] = {f->tbuf.t[i], 0};
      apwbuf(&f->wbuf, c);
    }

    col++;
  }
}
void wowbuf(struct FInfo* f) {
  write(STDOUT_FILENO, f->wbuf.buf, f->wbuf.crp);
}
void wrtld(struct FInfo* f) {
  char c = '~';
  char* str = "    Commands: ^Q - Quit ^S - Save ^E - Erase Contents    ";
  for(int i = 0; i<f->t_w; i++) {
    write(STDOUT_FILENO, &c , 1);
  }
  mvcursor(f, 1, f->t_h);
  int lofeach = f->t_w/2;
  lofeach-=strlen(str)/2;
  for(int i = 0; i<lofeach; i++) {
    write(STDOUT_FILENO, &c, 1);
  }
  write(STDOUT_FILENO, str, strlen(str));
  for(int i = strlen(str)+lofeach; i<f->t_w; i++) {
    write(STDOUT_FILENO, &c, 1);
  }
  return;
}
int main(int argc, char*argv[]) {
  save_term();
  setup_term();
  int c;
  struct FInfo fi = {0};
  fi.cx = 1;
  fi.cy = 1;
  if(argc>1) {
    fi.fp=fopen(argv[1], "r+");
    if(fi.fp==NULL) {
      if(errno==ENOENT) {
        fi.fp = fopen(argv[1], "w+");
        goto cont;
      }
      reset_term();
      printf("Error opening file: ");
      handle_file_error(errno);
      exit(1);
    }
  } else {
    reset_term();
    printf("Please pass a file path: editor <path>");
    exit(1);
  }
 cont:

  
  get_win_size(&fi);
  fi.wbuf.buf = (char*)malloc(sizeof(char)*(fi.t_w*fi.t_h*2));
  fi.path = argv[1];
  if (!fi.wbuf.buf) {
    reset_term();
    printf("Error allocating memory for WBUF.\n");
    fclose(fi.fp);
    exit(1);
  }
  fi.wbuf.size = sizeof(char)*(fi.t_w*fi.t_h*2);
  load_tbuf(&fi);
  write_to_wbuf(&fi);
  clear_term(&fi);
  write_to_wbuf_ft(&fi);
  wowbuf(&fi);
  while(1) {
    if(fi.cx < 1) {
      fi.cx = 1;
    }
    mvcursor(&fi, fi.cx - fi.coloff, fi.cy - fi.rowoff);
    handle_char(&fi, read_char(&fi));
    scroll(&fi);
    if (fi.fl[FCHANG]) {
        clear_term(&fi);
        write_to_wbuf_ft(&fi);
        wowbuf(&fi);
        fi.fl[FCHANG] = 0;
    }
    if(fi.fl[EXQUIT]==1) {
      reset_term();
      fclose(fi.fp);
      printf("Used a terminal with %d columns and %d rows.", fi.t_w, fi.t_h);
      free(fi.wbuf.buf);
      exit(0);
    }
    get_win_size(&fi);
  }
  free(fi.tbuf.t);
  return 0;
}
