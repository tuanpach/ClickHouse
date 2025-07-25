#pragma once
#include <Processors/QueryPlan/IQueryPlanStep.h>

namespace DB
{

/// Unite several logical streams of data into single logical stream with specified structure.
class UnionStep : public IQueryPlanStep
{
public:
    /// max_threads is used to limit the number of threads for result pipeline.
    explicit UnionStep(SharedHeaders input_headers_, size_t max_threads_ = 0);

    String getName() const override { return "Union"; }

    QueryPipelineBuilderPtr updatePipeline(QueryPipelineBuilders pipelines, const BuildQueryPipelineSettings & settings) override;

    void describePipeline(FormatSettings & settings) const override;

    size_t getMaxThreads() const { return max_threads; }

    void serialize(Serialization & ctx) const override;
    bool isSerializable() const override { return true; }

    static std::unique_ptr<IQueryPlanStep> deserialize(Deserialization & ctx);

    bool hasCorrelatedExpressions() const override { return false; }

private:
    void updateOutputHeader() override;

    size_t max_threads;
};

}
