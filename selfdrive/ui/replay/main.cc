#include <QApplication>

#include "replay.hpp"

int main(int argc, char *argv[]){
  QApplication a(argc, argv);

  QString route(argv[1]);
  if (route == "") {
    printf("Usage: ./replay \"route\"\n");
    return 1;
  }

  Replay *replay = new Replay(route, 300);
  replay->stream();

  return a.exec();
}