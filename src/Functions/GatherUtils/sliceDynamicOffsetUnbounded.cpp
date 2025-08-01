#ifndef __clang_analyzer__ // It's too hard to analyze.

#include <Functions/GatherUtils/GatherUtils.h>
#include <Functions/GatherUtils/Selectors.h>
#include <Functions/GatherUtils/Algorithms.h>

namespace DB::GatherUtils
{

namespace
{

struct SliceDynamicOffsetUnboundedSelectArraySource
        : public ArraySourceSelector<SliceDynamicOffsetUnboundedSelectArraySource>
{
    template <typename Source>
    static void selectSource(bool is_const, bool is_nullable, Source && source, const IColumn & offset_column, ColumnArray::MutablePtr & result)
    {
        using SourceType = typename std::decay<Source>::type;
        using Sink = typename SourceType::SinkType;

        if (is_nullable)
        {
            using NullableSource = NullableArraySource<SourceType>;
            using NullableSink = typename NullableSource::SinkType;

            auto & nullable_source = static_cast<NullableSource &>(source);

            result = ColumnArray::create(nullable_source.createValuesColumn());
            NullableSink sink(result->getData(), result->getOffsets(), source.getColumnSize());

            if (is_const)
                sliceDynamicOffsetUnbounded(static_cast<ConstSource<NullableSource> &>(source), sink, offset_column);
            else
                sliceDynamicOffsetUnbounded(static_cast<NullableSource &>(source), sink, offset_column);
        }
        else
        {
            result = ColumnArray::create(source.createValuesColumn());
            Sink sink(result->getData(), result->getOffsets(), source.getColumnSize());

            if (is_const)
                sliceDynamicOffsetUnbounded(static_cast<ConstSource<SourceType> &>(source), sink, offset_column);
            else
                sliceDynamicOffsetUnbounded(source, sink, offset_column);
        }
    }
};

}

ColumnArray::MutablePtr sliceDynamicOffsetUnbounded(IArraySource & src, const IColumn & offset_column)
{
    ColumnArray::MutablePtr res;
    SliceDynamicOffsetUnboundedSelectArraySource::select(src, offset_column, res);
    return res;
}
}

#endif
