'''
A linear regression learning algorithm example using TensorFlow library.

Author: Aymeric Damien
Project: https://github.com/aymericdamien/TensorFlow-Examples/
'''

from __future__ import print_function

import unittest
import tensorflow as tf
import numpy as np
import numpy.testing as npt
from ddt import ddt, data, unpack


def run_linear_reg(sess):
    # Parameters
    learning_rate = 0.01
    training_epochs = 100

    # Training Data
    train_X = np.asarray([3.3,4.4,5.5,6.71,6.93,4.168,9.779,6.182,7.59,2.167,
                          7.042,10.791,5.313,7.997,5.654,9.27,3.1])
    train_Y = np.asarray([1.7,2.76,2.09,3.19,1.694,1.573,3.366,2.596,2.53,1.221,
                          2.827,3.465,1.65,2.904,2.42,2.94,1.3])

    n_samples = train_X.shape[0]

    # tf Graph Input
    X = tf.placeholder(tf.float32)
    Y = tf.placeholder(tf.float32)

    # Set model weights
    W = tf.Variable(np.random.randn(), name="weight")
    b = tf.Variable(np.random.randn(), name="bias")

    # Construct a linear model
    pred = tf.add(tf.multiply(X, W), b)

    # Mean squared error
    cost = tf.reduce_sum(tf.pow(pred - Y, 2)) / (2 * n_samples)
    # Gradient descent
    #  Note, minimize() knows to modify W and b because Variable objects are trainable=True by default
    optimizer = tf.train.GradientDescentOptimizer(learning_rate).minimize(cost)

    # Initializing the variables
    init = tf.global_variables_initializer()

    # Launch the graph
    sess.run(init)

    # Fit all training data
    for epoch in range(training_epochs):
        for (x, y) in zip(train_X, train_Y):
            sess.run(optimizer, feed_dict={X: x, Y: y})

    return sess.run(pred, feed_dict={X: 5.6})


@ddt
class TestLinreg(unittest.TestCase):

    def test_linear_regression(self):
        tf.reset_default_graph()
        tf.set_random_seed(233)
        np.random.seed(233)
        with tf.device('/device:RPC:0'):
            with tf.Session() as sess:
                actual = run_linear_reg(sess)

        tf.reset_default_graph()
        tf.set_random_seed(233)
        np.random.seed(233)

        with tf.device('/device:CPU:0'):
            with tf.Session() as sess:
                expected = run_linear_reg(sess)

        self.assertEquals(actual, expected)
