#ifndef TESTS_TESTDATANODE_H
#define TESTS_TESTDATANODE_H

#include "DataNode.h"

/** Concrete stack node for DataBroker characterization tests. */
class TestDataNode final : public DataNode {
  public:
    ~TestDataNode() = default;
};

#endif
