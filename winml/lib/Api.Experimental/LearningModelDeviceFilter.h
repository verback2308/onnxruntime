#pragma once
#include "LearningModelDeviceFilter.g.h"

namespace WINML_EXPERIMENTALP
{
    struct LearningModelDeviceFilter : LearningModelDeviceFilterT<LearningModelDeviceFilter>
    {
        LearningModelDeviceFilter();

        winml_experimental::LearningModelDeviceFilter IncludeAll();

        winml_experimental::LearningModelDeviceFilter Include(
            winml::LearningModelDeviceKind const& strategy);

        winml_experimental::LearningModelDeviceFilter Clear();

        wfc::IIterator<winml::LearningModelDeviceKind> First();

        winml::LearningModelDeviceKind GetAt(uint32_t index);

        uint32_t Size();

        bool IndexOf(winml::LearningModelDeviceKind const& value, uint32_t& index);

        uint32_t GetMany(
            uint32_t startIndex,
            array_view<winml::LearningModelDeviceKind> items);

    private:
        std::vector<winml::LearningModelDeviceKind> device_kinds_;

    };
}
