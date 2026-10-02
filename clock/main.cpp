#include "dynload.hpp"
#include "gl_loader.hpp"

#define _MILSKO
#include <Mw/Milsko.h>
#include <Mw/Widget/OpenGL.h>

#include <time.h>

static struct {
  MwWidget window, hbox, opengl, date, time;
  time_t last = 0;
  struct tm *tm;
  int br, bg, bb;
  int fr, fg, fb;
} ctx;

double deg2rad(double deg) { return (360.0 - deg) / 180.0 * M_PI; }

void MWAPI tick(MwWidget handle, void *user, void *call) {
  time_t t = time(NULL);
  int i;
  double rad;
  int render = 0;
  int w = MwGetInteger(ctx.opengl, MwNwidth);
  int h = MwGetInteger(ctx.opengl, MwNheight);

  (void)handle;
  (void)user;
  (void)call;

  if (ctx.last != t) {
    const char *wday[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    const char *mon[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                         "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

    char buf[512];
    ctx.tm = localtime(&t);

    MwStringPrintIntoBuffer(buf, 512, "%s %02d %s", mon[ctx.tm->tm_mon],
                            ctx.tm->tm_mday, wday[ctx.tm->tm_wday]);
    MwSetString(ctx.date, MwNtext, buf);

    MwStringPrintIntoBuffer(buf, 512, "%02d:%02d %s", ctx.tm->tm_hour % 12,
                            ctx.tm->tm_min,
                            ctx.tm->tm_hour >= 12 ? "PM" : "AM");
    MwSetString(ctx.time, MwNtext, buf);

    render = 1;

    ctx.last = t;
  }

  MwOpenGLMakeCurrent(ctx.opengl);

  glClearColor(ctx.br / 255.0, ctx.bg / 255.0, ctx.bb / 255.0, 1);
  glClear(GL_COLOR_BUFFER_BIT);

  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  glOrtho(-1, 1, -1, 1, 0, 100);

  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();

  glLineWidth(2);
  for (i = 0; i < 12; i++) {
    rad = deg2rad(30.0 * i);
    glPointSize((i % 3) ? 2 : 5);
    glBegin(GL_POINTS);
    glColor3f(ctx.fr / 255.0, ctx.fg / 255.0, ctx.fb / 255.0);
    glVertex2f(cos(rad) * 0.85, sin(rad) * 0.85);
    glEnd();
  }

  rad = deg2rad((ctx.tm->tm_hour % 12) * 30 + (ctx.tm->tm_min / 2) - 90);
  glBegin(GL_LINES);
  glVertex2f(0, 0);
  glVertex2f(cos(rad) * 0.3, sin(rad) * 0.3);
  glEnd();

  rad = deg2rad(ctx.tm->tm_min * 6 - 90);
  glBegin(GL_LINES);
  glVertex2f(0, 0);
  glVertex2f(cos(rad) * 0.5, sin(rad) * 0.5);
  glEnd();

  if (render && w > 0 && h > 0) {
    unsigned char *buffer = new unsigned char[w * h * 4];
    MwPixmap px;
    int j;

    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, buffer);
    for (i = 0; i < h / 2; i++) {
      for (j = 0; j < w * 4; j++) {
        unsigned char b = buffer[i * w * 4 + j];

        buffer[i * w * 4 + j] = buffer[(h - i - 1) * w * 4 + j];
        buffer[(h - i - 1) * w * 4 + j] = b;
      }
    }

    px = MwLoadRaw(ctx.window, buffer, w, h);
    MwVaApply(ctx.window, MwNiconPixmap, px, NULL);
    MwDestroyPixmap(px);

    free(buffer);
  }

  MwOpenGLSwapBuffer(ctx.opengl);
}

void MWAPI resize(MwWidget handle, void *user, void *call) {
  int w = MwGetInteger(handle, MwNwidth);
  int h = MwGetInteger(handle, MwNheight);

  (void)user;
  (void)call;

  w = MwGetInteger(ctx.opengl, MwNwidth);
  h = MwGetInteger(ctx.opengl, MwNheight);
  glViewport(0, 0, w, h);
}

int main() {
  MwColor bgcolor, fgcolor;
  MwRect bounds;

  if (!dynload_setup::gl()) {
    return 1;
  }

  MwLibraryInit();

  ctx.window = MwVaCreateWidget(MwWindowClass, "main", NULL, MwDEFAULT,
                                MwDEFAULT, 250, 300, MwNtitle, "clock", NULL);
  MwGetScreenSize(ctx.window, &bounds);
  MwShow(ctx.window, 0);

  bgcolor = MwParseColor(ctx.window, MwGetString(ctx.window, MwNbackground));
  MwColorGet(bgcolor, &ctx.br, &ctx.bg, &ctx.bb);

  fgcolor = MwParseColor(ctx.window, MwGetString(ctx.window, MwNforeground));
  MwColorGet(fgcolor, &ctx.fr, &ctx.fg, &ctx.fb);

  ctx.opengl = MwVaCreateWidget(MwOpenGLClass, "clock", ctx.window, 0, 50, 250,
                                250, MwNratio, 6, NULL);
  ctx.hbox = MwVaCreateWidget(MwBoxClass, "box", ctx.window, 0, 0, 250, 50,
                              MwNorientation, MwHORIZONTAL, NULL);
  ctx.date = MwVaCreateWidget(MwLabelClass, "date", ctx.hbox, 100, 0, 150, 50,
                              MwNbold, 1, NULL);
  ctx.time = MwVaCreateWidget(MwLabelClass, "time", ctx.hbox, 100, 50, 150, 50,
                              MwNbold, 1, NULL);

  MwAddUserHandler(ctx.window, MwNtickHandler, tick, NULL);
  MwAddUserHandler(ctx.window, MwNresizeHandler, resize, NULL);

  MwLLBeginStateChange(ctx.window->lowlevel);
  MwLLMakeToolWindow(ctx.window->lowlevel);
  MwLLEndStateChange(ctx.window->lowlevel);
  MwVaApply(ctx.window, MwNx, 50, MwNy, bounds.height - 50 - 300, NULL);

  MwShow(ctx.window, 1);
  MwStep(ctx.window);
  resize(ctx.window, NULL, NULL);
  tick(ctx.window, NULL, NULL);

  MwLoop(ctx.window);

  MwFreeColor(fgcolor);
  MwFreeColor(bgcolor);
}
