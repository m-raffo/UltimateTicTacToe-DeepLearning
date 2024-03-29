using namespace std;

#include <vector>
#include <utility>
#include <thread>
#include <mutex>
#include <BatchManager.h>
#include <exception>
#include <vector>
#include <GameState.h>
#include <Minimax.h>
#include <chrono>
#include <iostream>
#include <queue>
#include <deque>

#define QUEUE_CHECK_DELAY       0.5ms

int ongoingGames;


mutex mtx;
queue<batch> needsEvaluation;
vector<vector<batch>> fromNN;
mutex fromNNmtx[MAX_THREADS];

thread *mctsThreads[MAX_THREADS];


mutex resultsMTX;
deque<vector<trainingExampleVector>> resultsHistory;
vector<trainingExampleVector> results;

float RandomFloat(float a, float b) {
    float random = ((float) rand()) / (float) RAND_MAX;
    float diff = b - a;
    float r = random * diff;
    return a + r;
}

int RandomActionWeighted(vector<float> weights) {
    int action = 0;

    float weightSum = 0;
    // Pick a random move based on the weights
    for (float f : weights) {
        weightSum += f;
    }

    float rnd = RandomFloat(0, weightSum);

    for (float f : weights) {
        // Stop at the right action
        if (rnd <= f) {
            return action;
        }

        action++;
        rnd -= f;
    }

    // Throw exception because execution should always return before this

}

int MaxAction(vector<float> probs) {
    int action;
    float bestProbs = -1;

    int index = 0;

    for (float f : probs) {
        if (f > bestProbs) {
            action = index;
            bestProbs = f;
        }

        index++;
    }

    return action;
}

BatchManager::BatchManager() {
}

BatchManager::BatchManager(int _batchSize, int _numThreads, float _cpuct, int _numSims, double _dirichlet_a, float _dirichlet_x, float _percent_q) {
    batchSize = _batchSize;
    cpuct = _cpuct;
    numSims = _numSims;
    dirichlet_a = _dirichlet_a;
    dirichlet_x = _dirichlet_x;
    percent_q = _percent_q;
    if (_numThreads > 0 && _numThreads <= MAX_THREADS) {
        numThreads = _numThreads;
    }
}

void simple() {
    // this_thread::sleep_for(1000ms);
    for (int i = 0 ; i < 4; i++) {
    // cout << "Working\n";

    this_thread::sleep_for(100ms);

    }
}

void mctsWorker(int workerID, BatchManager *parent) {

    #ifdef PROFILE_ITERATIONS
    int iterationsComplete = 0;
    int targetIterations = PROFILE_ITERATIONS;
    #endif

    cout << "Thread " << workerID << " created!\n";

    vector<MCTS> episodes;

    // Start all of the episodes
    for (int i = 0; i < parent->batchSize; i++) {
        episodes.push_back(MCTS(parent->cpuct, parent->dirichlet_a, parent->percent_q));

    }

    for (MCTS &ep : episodes) {
        ep.startNewSearch(GameState());
    }

    mtx.lock();
    ongoingGames += parent->batchSize;
    mtx.unlock();

    int actionsTaken = 0;
    int remainingGames = episodes.size();

    // auto t1 = chrono::steady_clock::now();
    // auto t2 = chrono::steady_clock::now();


    while(remainingGames > 0) {

        // Run all MCTS sims
        for (int i = 0; i < parent->numSims; i++) {
            batch needsEval;
            needsEval.workerID = workerID;

            // Prepare Batch
            for (MCTS &ep : episodes) {
                if (ep.gameOver) {
                    continue;
                }
                // TODO: Reduce copying that is performed here
                board2D newEval = ep.searchPreNN();

                if (ep.evaluationNeeded) {
                    needsEval.canonicalBoards.push_back(newEval);
                }
            }

            // t2 = chrono::steady_clock::now();
            // cout << "Batch creation took " << (float)chrono::duration_cast<std::chrono::milliseconds>(t2-t1).count()  << " milliseconds\n";

            // Posting evaluation requires the lock
            mtx.lock();
            needsEvaluation.push(needsEval);
            mtx.unlock();

            // Wait for the result
            // auto t3 = chrono::steady_clock::now();

            while (true) {

                // If the result is available, get it
                fromNNmtx[workerID].lock();
                if (fromNN[workerID].size() > 0) {
                    needsEval = fromNN[workerID].back();
                    fromNN[workerID].pop_back();
                    fromNNmtx[workerID].unlock();
                    // cout << "GOT RESULTS BACK\n";
                    // t1 = chrono::steady_clock::now();
                    break;
                }

                // cout << "Results are not back\n";

                fromNNmtx[workerID].unlock();

                // Wait until checking again
                this_thread::sleep_for(QUEUE_CHECK_DELAY);
            }

            // auto t4 = chrono::steady_clock::now();
            // cout << "Waited for results for " << (float)chrono::duration_cast<std::chrono::milliseconds>(t4-t3).count()  << " milliseconds\n";


            // cout << "Broken out of loop EP SIZE " << episodes.size() << '\n';


            // Batch results
            int resultsIndex = 0;
            for (MCTS &ep : episodes) {

                if (ep.gameOver) {
                    continue;
                }

                // Skip if no evaluation was needed
                if (!ep.evaluationNeeded) {
                    continue;
                }

                // cout << "Starting search post nn\n";

                // ep.currentNode->board.displayGame();

                // cout << "Game diplsayed\n";

                ep.searchPostNN(needsEval.pis[resultsIndex], needsEval.evaluations[resultsIndex]);

                // cout << "Finishing search post nn\n";

                // Move to the next set of results only if an evaluation was needed
                resultsIndex += 1;
            }

        }

        actionsTaken++;

        // Make moves
        for (MCTS &ep : episodes) {
            if (ep.gameOver) {
                continue;
            }
            vector<float> probs = ep.getActionProb();

            // Save probability before adding noise

            ep.saveTrainingExample(probs, ep.rootNode.w / ep.rootNode.n);

            int action;

            if (actionsTaken < TEMP_THRESHOLD) {
                // Add dirichlet noise
                int numActions = ep.rootNode.children.size();
                vector<double> dir = ep.dir(parent->dirichlet_a, numActions);
                int i = 0;
                for (float &prob : probs) {
                    if (prob != 0) {
                        prob = parent->dirichlet_x * prob + (1 - parent->dirichlet_x) * dir[i];
                        i++;
                    }
                }
        
                action = RandomActionWeighted(probs);
            } else {
                action = MaxAction(probs);
            }

            ep.takeAction(action);
            

            // ep.displayGame();

            int status = ep.getStatus();

            // Save the examples if the game is over
            if (status != 0) {

                // Replace status with the result value {1, -1, 0}
                if (status == 3) {
                    status = 0;
                } else {
                    status = status == 1 ? 1 : -1;
                }

                // Save all the examples
                resultsMTX.lock();
                for (trainingExampleVector ex : ep.getTrainingExamplesVector(status)) {
                    results.push_back(ex);
                }
                resultsMTX.unlock();

                ep.gameOver = true;
                remainingGames--;
                mtx.lock();
                ongoingGames--;
                mtx.unlock();
            }
        }

        #ifdef PROFILE_ITERATIONS
        iterationsComplete++;
        if (iterationsComplete >= targetIterations) {
            cout << "Profile iterations complete\n";
            mtx.lock();
            ongoingGames -= episodes.size();
            mtx.unlock();
            break;
        }
        #endif

        

    }
}

void BatchManager::createMCTSThreads() {
    for (int i = 0; i < numThreads; i++) {

        vector<batch> fromNNVector;

        // Vector for sending results back
        fromNN.push_back(fromNNVector);
        // Thread the do the work
        mctsThreads[i] = new thread(mctsWorker, i, this);
        // mctsThreads[i] = new thread(simple);

        // mctsThreads[0].join();

        // mctsThreads[i].join();

    }
}

void BatchManager::stopMCTSThreads() {

    for (int i = 0; i < MAX_THREADS; i++) {
        mctsThreads[i]->join();
    }
}

int BatchManager::getBatchSize() {
    mtx.lock();
    int result = needsEvaluation.size();
    mtx.unlock();
    return result;
}

batch BatchManager::getBatch() {
    mtx.lock();
    if (needsEvaluation.size() > 0) {
        batch newBatch = needsEvaluation.front();
        needsEvaluation.pop();
        mtx.unlock();
        return newBatch;
    }

    mtx.unlock();

    cout << "Warning :: Empty batch returned!\n";
    // TODO: Throw exception if the size is < 1 
}

void BatchManager::putBatch(batch evaluation) {
    fromNNmtx[evaluation.workerID].lock();
    fromNN[evaluation.workerID].push_back(evaluation);
    fromNNmtx[evaluation.workerID].unlock();
}

int BatchManager::getOngoingGames() {
    mtx.lock();
    int result = ongoingGames;
    mtx.unlock();
    return result;
}

void addExampleToTrainingVector(trainingExampleVector *existing, trainingExampleVector *newEx) {
    // Average each value of pi
    int previousWeight = existing->timesSeen;
    for (int i = 0; i < 81; i++) {
        existing->pi[i] = (existing->pi[i] * previousWeight + newEx->pi[i]) / (previousWeight + 1);
    }

    // Average the result value
    existing->result = (float)(existing->result * previousWeight + newEx->result) / (float)(previousWeight + 1);

    existing->timesSeen++;
}

void BatchManager::saveTrainingExampleHistory() {
    // Get the results and then clear them
    resultsMTX.lock();
    vector<trainingExampleVector> allExamples = results;
    results.clear();
    resultsMTX.unlock();

    resultsHistory.push_front(allExamples);

}

vector<trainingExampleVector> BatchManager::getTrainingExamples() {
    return getTrainingExamples(resultsHistory.size());
}

vector<trainingExampleVector> BatchManager::getTrainingExamples(int pastIterations) {

    if (pastIterations > resultsHistory.size()) {
        pastIterations = resultsHistory.size();
    }


    vector<trainingExampleVector> trimmedExamples;
    int foundIndex;

    for (int historyIndex = 0; historyIndex < pastIterations; historyIndex++) {
        for (trainingExampleVector &ex : resultsHistory[historyIndex]) {
            // Search for the board in the examples already found
            foundIndex = -1;
            trainingExampleVector exCanonical = getCanonicalTrainingExampleRotation(ex);

            for (int i = 0; i < trimmedExamples.size(); i++) {
                if (exCanonical.canonicalBoard == trimmedExamples[i].canonicalBoard) {
                    foundIndex = i;
                    break;
                }
            }

            // If the board is already found, update the existing position
            if (foundIndex > -1) {
                addExampleToTrainingVector(&(trimmedExamples[foundIndex]), &exCanonical);
            }
            // If new position, add it for the first time
            else {
                trimmedExamples.push_back(exCanonical);
            }
        }
    }
        

    // Get all possible rotations of the trimmed examples
    vector<trainingExampleVector>  allExamples;
    for (trainingExampleVector &ex : trimmedExamples) {
        vector<trainingExampleVector> rotations = getSymmetries(ex);

        allExamples.insert(allExamples.end(), rotations.begin(), rotations.end());
    }

    return allExamples;
}

void BatchManager::purgeHistory(int iterationsToSave) {
    while (resultsHistory.size() > iterationsToSave) {
        resultsHistory.pop_back();
    }
}
