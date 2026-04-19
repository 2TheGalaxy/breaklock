#define ASIO_STANDALONE
#include <algorithm>
#include <asio.hpp>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using asio::ip::tcp;
using Combination = std::vector<int>;

/**
 * @brief Structure storing the history of sent queries.
 * We keep the history of ALL queries (even from Phase 1) to ruthlessly 
 * filter out invalid permutations in Phase 2.
 */
struct History
{
  Combination query;
  int hits;
  int exact;
};

/**
 * @brief Structure storing the decoded response from the server.
 */
struct Response
{
  bool solved;
  int hits;
  int exact;
  int attempts;
};

/**
 * @brief Sends a "TRY" query with the given combination and parses the server's response.
 */
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
    std::cout << "[VICTORY] Solved in " << r.attempts << " attempts!\n";
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
    std::cerr << "Usage: " << argv[0] << " <host> <port>\n";
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

    // Read session header parameters from the server
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
      std::cerr << "Error: Failed to extract LENGTH from the server.\n";
      return 1;
    }

    std::cout << "[INFO] Playing on a " << n << "x" << n << " board\n";

    // =================================================================
    // WRAPPER: AUTOMATIC HISTORY TRACKING & EXIT ON WIN
    // =================================================================
    std::vector<History> history;

    auto my_send_query = [&](const Combination& q)
    {
      auto res = send_query(socket, q);
      if (res.solved)
      {
        exit(0); // Instantly terminate the program upon victory
      }
      history.push_back({q, res.hits, res.exact});
      return res;
    };

    // =================================================================
    // PHASE 1: FIND EXACTLY THE 10 CORRECT NUMBERS (DIVIDE & CONQUER)
    // =================================================================
    std::vector<Combination> blocks(n);
    std::vector<int> H(n);
    std::vector<int> known_zeros;

    // 1A. Divide the 100 numbers into 10 blocks of 10. Query each block.
    for (int i = 0; i < n; ++i)
    {
      for (int j = 1; j <= n; ++j)
      {
        blocks[i].push_back(i * n + j);
      }
      auto res = my_send_query(blocks[i]);
      H[i] = res.hits;

      // If a block has 0 hits, we just found 10 perfectly safe "zero/padding" numbers!
      if (res.hits == 0 && known_zeros.empty())
      {
        known_zeros = blocks[i];
      }
    }

    // 1B. Fallback: If no block had 0 hits, we find zeros through random sampling.
    // Statistically, a random 10-number query on a 10x10 board has a ~34% chance
    // of containing zero hits. This loop usually finishes in 2 to 4 queries.
    while (known_zeros.empty())
    {
      Combination q;
      while ((int)q.size() < n)
      {
        int r = (std::rand() % (n * n)) + 1;
        if (std::find(q.begin(), q.end(), r) == q.end())
          q.push_back(r);
      }
      auto res = my_send_query(q);
      if (res.hits == 0)
        known_zeros = q;
    }

    std::vector<int> true_numbers;

    // 1C. Recursive Binary Search to extract exact hits from subsets.
    // We pad subsets with our 'known_zeros' to isolate them and check exactly how many
    // correct numbers reside within that specific subset.
    auto extract_hits = [&](auto& self, const std::vector<int>& subset,
                            int hits_in_subset) -> void
    {
      // Base Case 1: No correct numbers in this subset. Drop it.
      if (hits_in_subset == 0)
        return;

      // Base Case 2: Every number in this subset is a hit. Save all of them!
      if (hits_in_subset == (int)subset.size())
      {
        for (int x : subset)
          true_numbers.push_back(x);
        return;
      }

      // Split the subset in half
      int mid = subset.size() / 2;
      std::vector<int> left(subset.begin(), subset.begin() + mid);
      std::vector<int> right(subset.begin() + mid, subset.end());

      // Query the left half, padded with known zeros to satisfy the length requirement 'n'
      Combination q = left;
      for (size_t i = 0; i < n - left.size(); ++i)
      {
        q.push_back(known_zeros[i]);
      }

      auto res = my_send_query(q);
      int left_hits = res.hits;
      int right_hits =
          hits_in_subset - left_hits; // Math deduction saves us a query!

      // Recurse deeper into both halves
      self(self, left, left_hits);
      self(self, right, right_hits);
    };

    // Execute the extraction process for every initial block that had > 0 hits
    for (int i = 0; i < n; ++i)
    {
      extract_hits(extract_hits, blocks[i], H[i]);
    }

    if ((int)true_numbers.size() != n)
    {
      std::cerr << "Logic error: Expected " << n << " true numbers, but found "
                << true_numbers.size() << "\n";
      return 1;
    }

    // =================================================================
    // PHASE 2: FIND THE EXACT PERMUTATION (LAZY MASTERMIND ALGORITHM)
    // =================================================================
    Combination p = true_numbers;
    std::sort(
        p.begin(),
        p.end()); // Crucial: required to initialize std::next_permutation properly

    bool space_exhausted = false;

    // Instead of holding 3.6 million permutations in a vector (which would crash/lag),
    // we advance 'p' in-place. We evaluate each of the 10! permutations at most ONCE.
    while (!space_exhausted)
    {
      bool ok = false;

      // Scan the permutation space for the FIRST sequence that doesn't contradict
      // the 'exact' match history we collected so far.
      while (true)
      {
        ok = true;
        for (const auto& h : history)
        {
          int exact = 0;
          for (int i = 0; i < n; ++i)
          {
            if (p[i] == h.query[i])
              exact++;
          }

          // If the permutation behaves differently regarding positional matches ('exact')
          // compared to historical server feedback, it's definitively wrong.
          if (exact != h.exact)
          {
            ok = false;
            break;
          }
        }

        // If it passes all historical checks, break the scan and shoot it at the server
        if (ok)
          break;

        // Advance to the next lexicographical permutation. If it wraps around to false, we checked them all.
        if (!std::next_permutation(p.begin(), p.end()))
        {
          space_exhausted = true;
          break;
        }
      }

      if (space_exhausted && !ok)
      {
        std::cerr << "Error: Search space exhausted with no consistent "
                     "permutation found.\n";
        return 1;
      }

      // Shoot the valid permutation. If it's the right one, my_send_query calls exit(0).
      my_send_query(p);

      // If it wasn't the solution, advance the permutation state by one so we don't re-test it next cycle.
      if (!std::next_permutation(p.begin(), p.end()))
      {
        space_exhausted = true;
      }
    }
  }
  catch (std::exception& e)
  {
    std::cerr << "Exception caught: " << e.what() << "\n";
  }

  return 0;
}
