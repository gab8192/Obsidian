// Obsidian.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include "cuckoo.h"
#include "evaluate.h"
#include "threads.h"
#include "tt.h"
#include "uci.h"

#include <cmath>
#include <functional>
#include <iomanip>
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>

struct BookEntry {
  Position pos;
  float wdl;
  std::vector<float> weightsCoeff;
};

constexpr int NumBookEntries = 999670;

BookEntry* bookEntries = new BookEntry[NumBookEntries];

void loadBook(std::vector<float>& weights) {
  std::fstream book("lichess-big3-resolved.book");
  std::string line;
  for (int i = 0; i < NumBookEntries; i++) {
    std::getline(book, line);
    int bracket = line.find('[');

    Position& pos = bookEntries[i].pos;
    pos.setToFen(line.substr(0, bracket));

    std::string wdl_str = line.substr(bracket + 1, 3);
    bookEntries[i].wdl = wdl_str == "0.5" ? 0.5 : 
               (wdl_str == "0.0" ? 0.0 : 1.0);

    bookEntries[i].weightsCoeff.resize(weights.size());

    Score eval = Eval::evaluate(pos, weights);

    for (int w = 0; w < weights.size(); w++) {
      float oldVal = weights[w];
      weights[w] = 0;
      float eval_zeroed = Eval::evaluate(pos, weights);
      weights[w] = oldVal;
      bookEntries[i].weightsCoeff[w] = (eval - eval_zeroed) / oldVal;
    }
  }
}

void printVector(const std::vector<float>& vec) {
  for (float v : vec) {
      std::cout << int(v) << ", ";
  }
  std::cout << std::endl;
}

class SGDOptimizer {
  public:
      using LossFunction = std::function<float(const std::vector<float>&, std::vector<float>&)>;
  
      SGDOptimizer(std::vector<float>& parameters, LossFunction loss_fn,
         float lr = 0.1f, float momentumCoeff = 0.9f, float velocityCoeff = 0.999)
          : params(parameters), loss_function(loss_fn), learning_rate(lr), beta1(momentumCoeff), beta2(velocityCoeff) {
      }
  
      void optimize(int iterations) {
          std::vector<float> velocity(params.size(), 0.0);
          std::vector<float> momentum(params.size(), 0.0);
          std::vector<float> gradients(params.size());

          for (int iter = 1; iter <= iterations; ++iter) {
              float loss = loss_function(params, gradients);

              printVector(params);
              std::cout << "Iteration " << iter << ", Loss: " << loss << "\n\n";

              for (size_t i = 0; i < params.size(); ++i) {
                  momentum[i] = beta1 * momentum[i] + (1.0 - beta1) * gradients[i];
                  velocity[i] = beta2 * velocity[i] + (1.0 - beta2) * gradients[i] * gradients[i];
                  float delta = learning_rate * momentum[i] / (1e-8 + sqrt(velocity[i]));
                  params[i] -= delta;
              }
          }
      }
  
  private:
      std::vector<float>& params;
      LossFunction loss_function;
      float learning_rate;
      float beta1;
      float beta2;
};

constexpr float K = 270.0;

float loss_fn(const std::vector<float>& weights, std::vector<float>& gradients) {
  float sumSqr = 0.0;
  std::fill(gradients.begin(), gradients.end(), 0.0f);
  
  for (int i = 0; i < NumBookEntries; i++) {
      Position& pos = bookEntries[i].pos;
      float wdl = bookEntries[i].wdl;

      Score eval = Eval::evaluate(pos, weights);
      float sigmoid = 1.0f / (1.0f + std::exp(-eval / K));
      float err = sigmoid - wdl;
      sumSqr += err * err;
      
      for (size_t w = 0; w < weights.size(); w++) {
          gradients[w] += err * bookEntries[i].weightsCoeff[w] * sigmoid * (1 - sigmoid);
      }
  }
  for (int w = 0; w < gradients.size(); w++)
    gradients[w] /= NumBookEntries;

  return sumSqr / NumBookEntries;
}

void tune() {

  std::vector<float> weights;
  Eval::registerWeights(weights);
  loadBook(weights);

  std::cout << "Ooaded training data" << std::endl;

  SGDOptimizer sgd(weights, loss_fn);
  sgd.optimize(1000);
}

int main(int argc, char** argv)
{
  std::cout << "Obsidian " << engineVersion << " by Gabriele Lombardo" << std::endl;

  Zobrist::init();

  Bitboards::init();

  positionInit();

  Cuckoo::init();

  Search::init();

  UCI::init();

  Threads::setThreadCount(UCI::Options["Threads"]);
  TT::resize(UCI::Options["Hash"]);

  NNUE::loadWeights();


 // tune();


  UCI::loop(argc, argv);

  Threads::setThreadCount(0);

  return 0;
}