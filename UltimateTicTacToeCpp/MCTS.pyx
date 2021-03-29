# distutils: language = c++

from Minimax cimport Node

cimport numpy as np

from cython.operator cimport dereference as deref

from libcpp.vector cimport vector
from libcpp.unordered_map cimport unordered_map

import numpy as np
import coloredlogs
import logging

from random import randint

cdef extern from "math.h":
    double sqrt(double m)

cdef class PyGameState:
    cdef GameState c_gamestate


    def __cinit__(self):
        self.c_gamestate = GameState()

    def get_copy(self):
        new_copy = PyGameState()
        new_copy.c_gamestate = self.c_gamestate.getCopy()

        return new_copy

    def move(self, board, piece):
        self.c_gamestate.move(board, piece)

    def get_status(self):
        return self.c_gamestate.getStatus()

    def get_board_status(self, board):
        return self.c_gamestate.getBoardStatus(board)

    def get_position(self, board, piece):
        return self.c_gamestate.getPosition(board, piece)

    def get_required_board(self):
        return self.c_gamestate.getRequiredBoard()

    def get_to_move(self):
        return self.c_gamestate.getToMove()

    def is_valid_move(self, board, piece):
        return bool(self.c_gamestate.isValidMove(board, piece))

cdef class PyMCTS:
    cdef MCTS mcts
    def __cinit__(self, cpuct):
        self.mcts = MCTS(cpuct)

    def startNewSearch(self, PyGameState position):
        self.mcts.startNewSearch(position.c_gamestate)

    def searchPreNN(self):
        return self.mcts.searchPreNN()

    def searchPostNN(self, vector[float] policy, float v):
        return self.mcts.searchPostNN(policy, v)
    
    @property
    def evaluationNeeded(self):
        return self.mcts.evaluationNeeded

    def getActionProb(self):
        return self.mcts.getActionProb()

    def takeAction(self, int action):
        self.mcts.takeAction(action)

    def getStatus(self):
        return self.mcts.getStatus()

    def displayGame(self):
        self.mcts.displayGame()

    def gameToString(self):
        return self.mcts.gameToString().decode('UTF-8')


def prepareBatch(trees):
    """
    Takes a python list of MCTS objects and returns a numpy array that needs to be 
    evaluated by the NN.
    """

    boards = np.zeros((len(trees), 199), dtype="int")

    cdef int [:, :] boardsView = boards

    cdef Node *evalNode 
    cdef int i
    cdef vector[int] canBoard

    cdef PyMCTS pymcts

    # Loop by reference
    for i in range(len(trees)):
        pymcts = trees[i]
        canBoard = pymcts.mcts.searchPreNN()

        for j in range(canBoard.size()):
            boardsView[i][j] = canBoard[j]

    return boards


def batchResults(trees, pi, v):
    """
    Takes a python list of MCTS objects and the numpy array of results and puts all
    of the results into the tree objects that need them.
    """
    cdef int index = 0
    cdef PyMCTS t


    for t in trees:
        if t.evaluationNeeded:
            t.searchPostNN(pi[index], v[index])
            index += 1


cdef testByReference(Node *n, int depth):
    cdef Node *startNode = n

    deref(startNode).addChildren()

    cdef int i 

    if depth == 0:
        for child in deref(startNode).children:
            child.board.displayGame()

    else:
        for i in range(deref(startNode).children.size()):
            testByReference(&deref(startNode).children[i], depth - 1)

def basicTest(PyGameState position, int depth):
    cdef Node start = Node(position.c_gamestate, 0)

    testByReference(&start, depth)
