#define ASIO_STANDALONE
#include <algorithm>
#include <asio.hpp> // Zwykłe Asio z folderu include/
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using asio::ip::tcp;
using Combination = std::vector<int>;

struct History
{
  Combination query;
  int hits;
  int exact;
};

struct Response
{
  bool solved;
  int hits;
  int exact;
  int attempts;
};

Response send_query(tcp::socket& socket, const Combination& query)
{
  std::string msg = "TRY";
  for (int x : query)
  {
    msg += " " + std::to_string(x);
  }
  msg += "\n";
  asio::write(socket, asio::buffer(msg));

  asio::streambuf buf;
  asio::read_until(socket, buf, '\n');
  std::istream is(&buf);
  std::string line;
  std::getline(is, line);
  if (!line.empty() && line.back() == '\r')
    line.pop_back();

  Response r = {false, 0, 0, 0};
  if (line.find("RESULT") == 0)
  {
    std::stringstream ss(line.substr(7));
    ss >> r.hits >> r.exact;
  }
  else if (line.find("SOLVED") == 0)
  {
    r.solved = true;
    std::stringstream ss(line.substr(7));
    ss >> r.attempts;
    std::cout << "[ZWYCIESTWO] Rozwiazano w " << r.attempts << " probach!\n";
  }
  else if (line.find("ERROR") == 0)
  {
    std::cerr << "SERVER ERROR: " << line << "\n";
    exit(1);
  }
  return r;
}

int main(int argc, char* argv[])
{
  if (argc != 3)
  {
    std::cerr << "Uzycie: " << argv[0] << " <host> <port>\n";
    return 1;
  }

  std::srand(static_cast<unsigned int>(std::time(nullptr)));

  try
  {
    asio::io_context io_context;
    tcp::resolver resolver(io_context);
    auto endpoints = resolver.resolve(argv[1], argv[2]);
    tcp::socket socket(io_context);
    asio::connect(socket, endpoints);

    // Odczyt parametrów z serwera
    asio::streambuf buf;
    std::istream is(&buf);
    std::string line;
    int n = 0;

    while (true)
    {
      asio::read_until(socket, buf, '\n');
      std::getline(is, line);
      if (!line.empty() && line.back() == '\r')
        line.pop_back();

      if (line.find("LENGTH ") == 0)
      {
        n = std::stoi(line.substr(7));
      }
      if (line == "ENDHELLO")
        break;
    }

    if (n == 0)
    {
      std::cerr << "Blad: Nie udalo sie wyciagnac LENGTH z serwera.\n";
      return 1;
    }

    std::cout << "[INFO] Gramy na planszy " << n << "x" << n << "\n";

    std::vector<History> history;
    std::vector<Combination> blocks(n);

    // Budujemy n bloków o rozmiarze n
    for (int i = 0; i < n; ++i)
    {
      for (int j = 1; j <= n; ++j)
      {
        blocks[i].push_back(i * n + j);
      }
    }

    // --- FAZA 1: Zapytania rozłączne ---
    std::vector<int> H(n);
    for (int i = 0; i < n; ++i)
    {
      auto res = send_query(socket, blocks[i]);
      if (res.solved)
        return 0;
      H[i] = res.hits;
      history.push_back({blocks[i], res.hits, res.exact});
    }

    std::vector<Combination> valid_sets;
    Combination current_set(n);

    // Rekurencyjne generowanie możliwych nieuporządkowanych setów
    auto build_sets = [&](auto& self, int block_idx, int current_size) -> void
    {
      if (block_idx == n)
      {
        if (current_size == n)
          valid_sets.push_back(current_set);
        return;
      }
      int k = H[block_idx];
      if (k == 0)
      {
        self(self, block_idx + 1, current_size);
        return;
      }
      std::string bitmask(k, 1);
      bitmask.resize(n, 0);
      do
      {
        int added = 0;
        for (int i = 0; i < n; ++i)
        {
          if (bitmask[i])
          {
            current_set[current_size + added] = blocks[block_idx][i];
            added++;
          }
        }
        self(self, block_idx + 1, current_size + k);
      } while (std::prev_permutation(bitmask.begin(), bitmask.end()));
    };

    build_sets(build_sets, 0, 0);

    // Filtrowanie setów, jeśli jest ich więcej niż 1
    while (valid_sets.size() > 1)
    {
      Combination best_query;
      if (valid_sets.size() > 500)
      {
        best_query = valid_sets[std::rand() % valid_sets.size()];
      }
      else
      {
        int min_max_part = 1e9;
        for (int i = 0; i < std::min(100, (int)valid_sets.size()); ++i)
        {
          const Combination& cand = valid_sets[i];
          std::map<int, int> part_counts;
          for (const auto& S : valid_sets)
          {
            int intersect = 0;
            for (int x = 0; x < n; ++x)
            {
              for (int y = 0; y < n; ++y)
              {
                if (S[x] == cand[y])
                {
                  intersect++;
                  break;
                }
              }
            }
            part_counts[intersect]++;
          }
          int max_part = 0;
          for (auto const& [hits_count, cnt] : part_counts)
            max_part = std::max(max_part, cnt);
          if (max_part < min_max_part)
          {
            min_max_part = max_part;
            best_query = cand;
          }
        }
      }

      auto res = send_query(socket, best_query);
      if (res.solved)
        return 0;
      history.push_back({best_query, res.hits, res.exact});

      std::vector<Combination> next_valid;
      for (const auto& S : valid_sets)
      {
        int intersect = 0;
        for (int x = 0; x < n; ++x)
        {
          for (int y = 0; y < n; ++y)
          {
            if (S[x] == best_query[y])
            {
              intersect++;
              break;
            }
          }
        }
        if (intersect == res.hits)
        {
          next_valid.push_back(S);
        }
      }
      valid_sets = std::move(next_valid);
    }

    if (valid_sets.empty())
    {
      std::cerr << "Blad logiczny: brak mozliwych kombinacji.\n";
      return 1;
    }

    // --- FAZA 2: Permutacje na wyselekcjonowanych cyfrach ---
    Combination true_numbers = valid_sets[0];
    std::vector<Combination> valid_perms;
    Combination p = true_numbers;
    std::sort(p.begin(), p.end());

    do
    {
      bool ok = true;
      for (const auto& h : history)
      {
        int exact = 0;
        for (int i = 0; i < n; ++i)
        {
          if (p[i] == h.query[i])
            exact++;
        }
        if (exact != h.exact)
        {
          ok = false;
          break;
        }
      }
      if (ok)
        valid_perms.push_back(p);
    } while (std::next_permutation(p.begin(), p.end()));

    while (valid_perms.size() > 1)
    {
      Combination best_query;
      if (valid_perms.size() > 500)
      {
        best_query = valid_perms[std::rand() % valid_perms.size()];
      }
      else
      {
        int min_max_part = 1e9;
        for (int i = 0; i < std::min(150, (int)valid_perms.size()); ++i)
        {
          const Combination& cand = valid_perms[i];
          std::map<int, int> part_counts;
          for (const auto& P : valid_perms)
          {
            int exact = 0;
            for (int j = 0; j < n; ++j)
            {
              if (P[j] == cand[j])
                exact++;
            }
            part_counts[exact]++;
          }
          int max_part = 0;
          for (auto const& [exact_count, cnt] : part_counts)
            max_part = std::max(max_part, cnt);
          if (max_part < min_max_part)
          {
            min_max_part = max_part;
            best_query = cand;
          }
        }
      }

      auto res = send_query(socket, best_query);
      if (res.solved)
        return 0;
      history.push_back({best_query, res.hits, res.exact});

      std::vector<Combination> next_perms;
      for (const auto& P : valid_perms)
      {
        int exact = 0;
        for (int j = 0; j < n; ++j)
        {
          if (P[j] == best_query[j])
            exact++;
        }
        if (exact == res.exact)
          next_perms.push_back(P);
      }
      valid_perms = std::move(next_perms);
    }

    if (!valid_perms.empty())
    {
      send_query(socket, valid_perms[0]);
    }
  }
  catch (std::exception& e)
  {
    std::cerr << "Wyjatek: " << e.what() << "\n";
  }

  return 0;
}
