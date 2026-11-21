PUBLISH_ALGORITHMS += entropyminimizer entropyminimizerv2 pitchraise resettorecording
TEST_ALGORITHMS += examplealgorithm

ALGORITHM_NAMES += $$PUBLISH_ALGORITHMS

CONFIG(debug, debug|release) {
    ALGORITHM_NAMES += $$TEST_ALGORITHMS
}
