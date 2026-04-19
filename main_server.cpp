// BreakLock server — C++17, standalone ASIO, wieloklientowy async TCP
// Wywołanie: breaklock_server <port> [board_size=4]

#include <algorithm>
#include <asio.hpp>
#include <atomic>
#include <chrono>
#include <iostream>
#include <memory>
#include <numeric>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using asio::ip::tcp;

class Session : public std::enable_shared_from_this<Session>
{
public:
  Session(tcp::socket socket, int n, std::atomic<uint64_t>& session_counter)
      : socket_(std::move(socket)), n_(n), attempts_(0), id_(++session_counter)
  {
    std::mt19937 rng(
        static_cast<uint32_t>(
            std::chrono::steady_clock::now().time_since_epoch().count()) ^
        static_cast<uint32_t>(id_));
    std::vector<int> pool(n_ * n_);
    std::iota(pool.begin(), pool.end(), 1);
    std::shuffle(pool.begin(), pool.end(), rng);
    secret_.assign(pool.begin(), pool.begin() + n_);
    log("connected  secret=" + fmt(secret_));
  }

  void start()
  {
    send_hello();
  }

private:
  tcp::socket socket_;
  asio::streambuf buf_;
  int n_, attempts_;
  std::vector<int> secret_;
  uint64_t id_;

  void log(const std::string& m) const
  {
    std::clog << "[session " << id_ << "] " << m << "\n";
  }
  static std::string fmt(const std::vector<int>& v)
  {
    std::ostringstream os;
    for (int i = 0; i < (int)v.size(); ++i)
    {
      if (i)
        os << ' ';
      os << v[i];
    }
    return os.str();
  }

  void async_send(std::string msg, std::function<void()> on_done = nullptr)
  {
    auto self = shared_from_this();
    auto buf = std::make_shared<std::string>(std::move(msg));
    asio::async_write(
        socket_, asio::buffer(*buf),
        [this, self, buf, on_done](std::error_code ec, std::size_t)
        {
          if (ec)
          {
            log("write error: " + ec.message());
            return;
          }
          if (on_done)
            on_done();
        });
  }

  void send_hello()
  {
    std::ostringstream os;
    os << "HELLO BREAKLOCK 1\n"
       << "BOARD " << n_ << " " << n_ << "\n"
       << "LENGTH " << n_ << "\n"
       << "NUMBERS 1 " << n_ * n_ << "\n"
       << "DISTINCT YES\n"
       << "ENDHELLO\n";
    auto self = shared_from_this();
    async_send(os.str(), [this, self] { read_line(); });
  }

  void read_line()
  {
    auto self = shared_from_this();
    asio::async_read_until(socket_, buf_, '\n',
                           [this, self](std::error_code ec, std::size_t)
                           {
                             if (ec)
                             {
                               log(ec == asio::error::eof
                                       ? "disconnected"
                                       : "read error: " + ec.message());
                               return;
                             }
                             std::istream is(&buf_);
                             std::string line;
                             std::getline(is, line);
                             if (!line.empty() && line.back() == '\r')
                               line.pop_back();
                             handle_line(line);
                           });
  }

  void handle_line(const std::string& line)
  {
    log("recv: " + line);
    if (line == "QUIT")
    {
      log("client quit");
      socket_.close();
      return;
    }

    std::istringstream iss(line);
    std::string cmd;
    if (!(iss >> cmd) || cmd != "TRY")
    {
      send_error(100, "BAD_FORMAT");
      return;
    }

    std::vector<int> dots;
    int x;
    while (iss >> x)
      dots.push_back(x);

    if ((int)dots.size() != n_)
    {
      send_error(103, "WRONG_LENGTH");
      return;
    }
    for (int d : dots)
      if (d < 1 || d > n_ * n_)
      {
        send_error(102, "OUT_OF_RANGE");
        return;
      }
    std::set<int> seen;
    for (int d : dots)
      if (!seen.insert(d).second)
      {
        send_error(101, "DUPLICATE_DOTS");
        return;
      }

    ++attempts_;
    int exact = 0, hits = 0;
    std::set<int> sset(secret_.begin(), secret_.end());
    for (int i = 0; i < n_; ++i)
    {
      if (dots[i] == secret_[i])
        ++exact;
      if (sset.count(dots[i]))
        ++hits;
    }

    if (exact == n_)
      send_solved();
    else
      send_result(hits, exact);
  }

  void send_result(int hits, int exact)
  {
    auto self = shared_from_this();
    async_send("RESULT " + std::to_string(hits) + " " + std::to_string(exact) +
                   "\n",
               [this, self] { read_line(); });
  }
  void send_solved()
  {
    auto self = shared_from_this();
    log("SOLVED in " + std::to_string(attempts_) + " attempts");
    async_send("SOLVED " + std::to_string(attempts_) + "\n",
               [this, self] { socket_.close(); });
  }
  void send_error(int code, const std::string& name)
  {
    auto self = shared_from_this();
    log("ERROR " + std::to_string(code) + " " + name);
    async_send("ERROR " + std::to_string(code) + " " + name + "\n",
               [this, self] { read_line(); });
  }
};

class Server
{
public:
  Server(asio::io_context& io, unsigned short port, int n)
      : acceptor_(io, tcp::endpoint(tcp::v4(), port)), n_(n), counter_(0)
  {
    std::clog << "[server] port=" << port << "  board=" << n << "x" << n
              << "\n";
    accept();
  }

private:
  tcp::acceptor acceptor_;
  int n_;
  std::atomic<uint64_t> counter_;

  void accept()
  {
    acceptor_.async_accept(
        [this](std::error_code ec, tcp::socket sock)
        {
          if (!ec)
            std::make_shared<Session>(std::move(sock), n_, counter_)->start();
          else
            std::clog << "[server] accept error: " << ec.message() << "\n";
          accept();
        });
  }
};

int main(int argc, char* argv[])
{
  if (argc < 2)
  {
    std::cerr << "Uzycie: breaklock_server <port> [board_size=4]\n";
    return 1;
  }
  unsigned short port = static_cast<unsigned short>(std::stoi(argv[1]));
  int n = (argc >= 3) ? std::stoi(argv[2]) : 4;
  if (n < 2 || n > 10)
  {
    std::cerr << "board_size: 2..10\n";
    return 1;
  }

  try
  {
    asio::io_context io;
    asio::signal_set sig(io, SIGINT, SIGTERM);
    sig.async_wait(
        [&](std::error_code, int s)
        {
          std::clog << "\n[server] sygnal " << s << " -- zamykam.\n";
          io.stop();
        });
    Server srv(io, port, n);
    io.run();
  }
  catch (std::exception& e)
  {
    std::cerr << "[fatal] " << e.what() << "\n";
    return 1;
  }
  return 0;
}
