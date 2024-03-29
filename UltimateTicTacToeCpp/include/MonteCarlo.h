#pragma once
using namespace std;

#include <bitset>
#include <vector>
#include <iostream>
#include <GameState.h>
#include <Minimax.h>
#include <limits>
#include <dirichlet.h>


struct trainingExample {
    bitset<199> canonicalBoard;
    float result;
    float pi[81];
    float q;
};

struct trainingExampleVector {
    vector<int> canonicalBoard;
    float result;
    float q;
    vector<float> pi;
    int timesSeen = 1;
};

struct trainingExample2D {
    int canonicalBoard[99][2] = {};
    float result;
    float q;
    vector<float> pi;
    int timesSeen = 1;
};

random_device rd;


class MCTS {
    float cpuct = 1;
    double dirichlet_a = 0.8;
    float percent_q = 0.5;

    vector<trainingExample> trainingPositions;

    mt19937 gen;
    dirichlet_distribution<mt19937> dirichlet;

    public:
    Node rootNode;
    Node *currentNode;


        MCTS();
        MCTS(float _cupct, double _dirichlet, float _percent_q);

        bool gameOver = false;


        void startNewSearch(GameState position);

        void backpropagate(Node *finalNode, float result);

        board2D searchPreNN();
        void searchPostNN(vector<float> policy, float v);

        bool evaluationNeeded;

        vector<float> getActionProb();
        void takeAction(int actionIndex);
        int getStatus();
        void displayGame();
        string gameToString();

        void saveTrainingExample(vector<float> pi, float q);
        vector<trainingExample> getTrainingExamples(int result);
        vector<trainingExampleVector> getTrainingExamplesVector(int result);
        void purgeTrainingExamples();

        vector<double> dir(double a, int dim);

};

vector<vector<int>> getSymmetriesBoard(vector<int> board);
vector<vector<float>> getSymmetriesPi(vector<float> pi);
vector<trainingExampleVector> getSymmetries(trainingExampleVector position);

/**
 * Takes a vector of training positions and converts them 
 * to their 2D equivalents.
 */
vector<trainingExample2D> convertTo2D(vector<trainingExampleVector> positions);

int findCanonicalRotation(vector<int> board);

vector<int> getCanonicalBoardRotation(vector<int> board);
trainingExampleVector getCanonicalTrainingExampleRotation(trainingExampleVector ex);
