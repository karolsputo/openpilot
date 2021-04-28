#include "replay.hpp"
#include <termios.h>
#include <iostream>

int getch(void) {
  int ch;
  struct termios oldt;
  struct termios newt;

  tcgetattr(STDIN_FILENO, &oldt);
  newt = oldt;
  newt.c_lflag &= ~(ICANON | ECHO);

  tcsetattr(STDIN_FILENO, TCSANOW, &newt);
  ch = getchar();
  tcsetattr(STDIN_FILENO, TCSANOW, &oldt);

  return ch;
}

Replay::Replay(QString route_, int seek_) : route(route_), seek(seek_) {
  unlogger = new Unlogger(&events, &events_lock, &frs, seek);
  current_segment = 0;
  bool create_jwt = true;

#if !defined(QCOM) && !defined(QCOM2)
  create_jwt = false;
#endif

  http = new HttpRequest(this, "https://api.commadotai.com/v1/route/" + route + "/files", "", create_jwt);
  QObject::connect(http, SIGNAL(receivedResponse(QString)), this, SLOT(parseResponse(QString)));
}

void Replay::parseResponse(QString response){
  response = response.trimmed();
  QJsonDocument doc = QJsonDocument::fromJson(response.toUtf8());

  if (doc.isNull()) {
    qDebug() << "JSON Parse failed";
    return;
  }

  camera_paths = doc["cameras"].toArray();
  log_paths = doc["logs"].toArray();

  // add first window:
  for(int i = 0 ; i < 3 ; i++) {
    int ind = seek/60 + i;
    if (ind < camera_paths.size()) {
      addSegment(seek/60 + i);
    }
  }
}

void Replay::addSegment(int i){
  if (lrs.find(i) != lrs.end()) {
    return;
  }

  QThread* thread = new QThread;

  QString log_fn = this->log_paths.at(i).toString();
  lrs.insert(i, new LogReader(log_fn, &events, &events_lock, &unlogger->eidx, i));

  lrs[i]->moveToThread(thread);
  QObject::connect(thread, SIGNAL (started()), lrs[i], SLOT (process()));
  thread->start();

  QString camera_fn = this->camera_paths.at(i).toString();
  frs.insert(i, new FrameReader(qPrintable(camera_fn)));
}

void Replay::trimSegment(){
	printf("%d\n", events.size());
	auto eit = events.begin();
	while(eit != events.end()){
		if(std::abs((*eit).first - current_segment) > 1){
			eit = events.erase(eit);
			continue;
		}
		eit++;
	}
	printf("%d\n", events.size());
}


void Replay::stream(SubMaster *sm){
  QThread* thread = new QThread;
  unlogger->moveToThread(thread);
  QObject::connect(thread, &QThread::started, [=](){
    unlogger->process(sm);
  });
  thread->start();

  QThread *seek_thread = new QThread;
  QObject::connect(seek_thread, &QThread::started, [=](){
    updateSeek();
  });
  seek_thread->start();

  QObject::connect(unlogger, &Unlogger::loadSegment, [=](){
    addSegment(++current_segment);
    trimSegment();
  });
}

void Replay::updateSeek(){
	char c;
  while(1){
    c = getch();
    if(c == '\n'){
			printf("Enter seek request: ");
			std::cin >> seek;
			// Add 3 segment window for seek
			for(int i = 0 ; i < 3 ; i++){
				int ind = seek/60 - 1 + i;
				if(std::abs(ind - current_segment) > 1){
					addSegment(ind);
				}
			}
			current_segment = seek/60;
			unlogger->setSeekRequest(seek*1e9);
			trimSegment();
			getch(); // remove \n from entering seek
    }
  }
}