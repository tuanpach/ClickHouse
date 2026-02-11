#pragma once

#include <DataTypes/IDataType.h>
#include <Interpreters/StorageID.h>
#include <Parsers/Prometheus/PrometheusQueryTree.h>
#include <Storages/TimeSeries/PrometheusQueryEvaluationRange.h>


namespace DB
{

struct PrometheusQueryEvaluationSettings
{
    using TimestampType = PrometheusQueryEvaluationRange::TimestampType;
    using DurationType = PrometheusQueryEvaluationRange::DurationType;

    StorageID time_series_storage_id = StorageID::createEmpty();
    DataTypePtr timestamp_data_type;
    DataTypePtr scalar_data_type;

    /// `evaluation_time` sets a specific time when the prometheus query is evaluated,
    /// `evaluation_range` sets a range of such times.
    /// If neither `evaluation_time` nor `evaluation_range` is set then the current time is used.
    /// The scale for these fields is the same as the scale used in `timestamp_data_type`.
    std::optional<TimestampType> evaluation_time;
    std::optional<PrometheusQueryEvaluationRange> evaluation_range;

    /// The window used by instant selectors (see lookback period).
    /// For example, query "http_requests_total @ 1770810669" is in fact evaluated as
    /// "last_over_time(http_requests_total[<instant_selector_window>] @ 1770810669)"
    /// If not set then it's 5 minutes by default.
    std::optional<DurationType> instant_selector_window;

    /// The default subquery step is used for subqueries specified without explicit step,
    /// for example "http_requests_total[10m:]"
    /// (If a step is given in the subquery, as in "http_requests_total[10m:1m]", then the given step is used.)
    /// If not set then it's 15 seconds by default.
    std::optional<DurationType> default_subquery_step;
};

}
