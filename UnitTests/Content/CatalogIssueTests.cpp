#include "Content/ContentDb.h"

#include <gtest/gtest.h>

using namespace Dark;

TEST(CatalogIssues, ReportedOnceAndKeptUntilCleared)
{
    clearCatalogIssues();
    reportCatalogIssue("Items", "no *.items.json found");
    reportCatalogIssue("Items", "no *.items.json found");
    reportCatalogIssue("ThrowableCatalog", "items/throwables.json is missing");
    ASSERT_EQ(catalogIssues().size(), 2u);
    EXPECT_EQ(catalogIssues()[0].packageId, "Items");
    clearCatalogIssues();
    EXPECT_TRUE(catalogIssues().empty());
}
