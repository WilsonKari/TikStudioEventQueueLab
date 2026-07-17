#pragma once

#include <cstddef>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace TikStudio::Tests
{
    using FTSTestFunction = void (*)();

    struct FTSTestCase
    {
        const char* Name = nullptr;
        FTSTestFunction Function = nullptr;
    };

    using FTSTestCases = std::vector<FTSTestCase>;

    inline void Require(
        bool bCondition,
        const std::string& Message
    )
    {
        if (!bCondition)
        {
            throw std::runtime_error(Message);
        }
    }

    [[nodiscard]]
    inline int RunTests(const FTSTestCases& Tests)
    {
        std::size_t PassedCount = 0;
        std::size_t FailedCount = 0;

        for (const FTSTestCase& Test : Tests)
        {
            try
            {
                Test.Function();
                ++PassedCount;
                std::cout << "PASS: " << Test.Name << '\n';
            }
            catch (const std::exception& Error)
            {
                ++FailedCount;
                std::cerr
                    << "FAIL: " << Test.Name
                    << " - " << Error.what()
                    << '\n';
            }
            catch (...)
            {
                ++FailedCount;
                std::cerr
                    << "FAIL: " << Test.Name
                    << " - unknown exception"
                    << '\n';
            }
        }

        std::cout
            << "RESULT: " << PassedCount << " passed, "
            << FailedCount << " failed\n";

        return FailedCount == 0 ? 0 : 1;
    }
}
