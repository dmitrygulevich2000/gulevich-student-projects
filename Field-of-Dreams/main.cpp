
#include <arpa/inet.h>
#include <fcntl.h>
#include <inttypes.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <unordered_map>
#include <string>
#include <vector>

const char SUCCESS_MSG[18] = "Congratulations!\n";

struct Game {
  std::string word;
  std::vector<bool> guessed;
  int attempts = 0;

  bool Complete(char letter);
  // update "guessed" and returns true if word is completed

  Game() = default;
  Game(const std::string& word, int attempts): word(word),
            guessed(word.size(), false),
            attempts(attempts) {}
};

bool Game::Complete(char letter) {
  bool completed = true;
  for (int i = 0; i < word.size(); ++i) {
    if (letter == word[i]) guessed[i] = true;
    if (!word[i]) completed = false;
  }

  return completed;
}

class GameServer {
 public:
  void Init(const std::string& ip, uint16_t port);
  void Run();

 private:
  void MakeNonblocking(int fd);
  void AddToEpoll(int fd, unsigned int events);
  void ProcessEvent(const epoll_event& event);

  Game NewGame();
  std::string GenerateWord();
  int Attempts(const std::string& word);

  void CloseConnection(int client);
  void ShutdownServer();

  static const int events_cnt_ = 1000;
  static const int buffer_size_ = 2048;

  int epoll_ = 0;
  int listening_socket_ = 0;
  epoll_event* events_ = nullptr;
  std::unordered_map<int, Game> games_;
  char buffer_[buffer_size_];
  bool stopped_ = false;
};

void GameServer::Init(const std::string& ip, uint16_t port) {
  stopped_ = false;
  listening_socket_ = socket(AF_INET, SOCK_STREAM, 0);
  MakeNonblocking(listening_socket_);
  listen(listening_socket_, SOMAXCONN);

  epoll_ = epoll_create(1);
  AddToEpoll(listening_socket_, EPOLLIN); // for accept events
  AddToEpoll(0, EPOLLIN);  // to shutdown

  events_ = new epoll_event[events_cnt_];
  printf("Press <enter> to stop server\n");
}

void GameServer::Run() {
  while (!stopped_ || !games_.empty()) {
    int events_now = epoll_wait(epoll_, events_, events_cnt_, -1);
    for (int i = 0; i < events_now; ++i) {
      ProcessEvent(events_[i]);
    }
  }

  ShutdownServer();
}


void GameServer::MakeNonblocking(int fd) {
  unsigned int flags = fcntl(fd, F_GETFL);
  fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

void GameServer::AddToEpoll(int fd, unsigned int events) {
  epoll_event event;
  event.events = events;
  event.data.fd = fd;

  epoll_ctl(epoll_, EPOLL_CTL_ADD, fd, &event);
}

void GameServer::CloseConnection(int client) {
  shutdown(client, SHUT_RDWR);
  close(client);

  games_.erase(client);
  epoll_ctl(epoll_, EPOLL_CTL_DEL, client, nullptr);
}

void GameServer::ShutdownServer() {
  for (const auto& i: games_) {
    CloseConnection(i.first);
  }
  close(listening_socket_);
  close(epoll_);
  delete[] events_;
}

std::string GameServer::GenerateWord() {
  return "hello";
}

int GameServer::Attempts(const std::string& word) {
  return word.size();
}

Game GameServer::NewGame() {
  std::string word = GenerateWord();
  return Game(word, Attempts(word));
}

void GameServer::ProcessEvent(const epoll_event &event) {
  if (event.events & EPOLLHUP) {
    CloseConnection(event.data.fd);

  } else {  // EPOLLIN
    if (event.data.fd == listening_socket_ && !stopped_) {
      int client = accept(listening_socket_, nullptr, nullptr);
      MakeNonblocking(client);
      AddToEpoll(client, EPOLLIN);
      games_.insert(std::make_pair(client, NewGame()));

    } else if (event.data.fd == 0) {  // end of work
      stopped_ = true;

    } else {  // client
      int bytes = read(event.data.fd, buffer_, buffer_size_);
      int completed = games_[event.data.fd].Complete(buffer_[0]);

      Game game = games_[event.data.fd];
      for (int i = 0; i < game.word.size(); ++i) {
        if (!game.guessed[i]) game.word[i] = '*';
      }

      write(event.data.fd, (game.word + '\n').data(), game.word.size());
      if (completed) {
        write(event.data.fd, SUCCESS_MSG, sizeof(SUCCESS_MSG));
        CloseConnection(event.data.fd);
      }
    }
  }
}

int main(int argc, char** argv) {
  if (argc < 4) {
    printf("No enough arguments,"
           " please pass mode[-server/-client], "
           "ip address and port");
    return 1;
  }

  if (strcmp(argv[1], "-server") == 0) {
    GameServer server;
    server.Init(argv[2], atoi(argv[3]));
    server.Run();
  } else if (strcmp(argv[1], "-client") == 0) {
    // TODO: implement client :(
  }

  return 0;
}