PRAGMA foreign_keys=OFF;

BEGIN TRANSACTION;

CREATE TABLE sources(source_id INTEGER PRIMARY KEY AUTOINCREMENT NOT NULL,source_event_id INTEGER NOT NULL,source_origin TEXT NOT NULL,reporting_origin TEXT NOT NULL,source_time INTEGER NOT NULL,expiry_time INTEGER NOT NULL,aggregatable_report_window_time INTEGER NOT NULL,num_attributions INTEGER NOT NULL,event_level_active INTEGER NOT NULL,aggregatable_active INTEGER NOT NULL,source_type INTEGER NOT NULL,attribution_logic INTEGER NOT NULL,priority INTEGER NOT NULL,source_site TEXT NOT NULL,debug_key INTEGER,remaining_aggregatable_attribution_budget INTEGER NOT NULL,num_aggregatable_attribution_reports INTEGER NOT NULL,aggregatable_source BLOB NOT NULL,filter_data BLOB NOT NULL,read_only_source_data BLOB NOT NULL,remaining_aggregatable_debug_budget INTEGER NOT NULL,num_aggregatable_debug_reports INTEGER NOT NULL,attribution_scopes_data BLOB,aggregatable_named_budgets BLOB);

CREATE TABLE reports(report_id INTEGER PRIMARY KEY AUTOINCREMENT NOT NULL,source_id INTEGER NOT NULL,trigger_time INTEGER NOT NULL,report_time INTEGER NOT NULL,initial_report_time INTEGER NOT NULL,failed_send_attempts INTEGER NOT NULL,external_report_id TEXT NOT NULL,debug_key INTEGER,context_origin TEXT NOT NULL,reporting_origin TEXT NOT NULL,report_type INTEGER NOT NULL,metadata BLOB NOT NULL,context_site TEXT NOT NULL);

CREATE TABLE rate_limits(id INTEGER PRIMARY KEY NOT NULL,scope INTEGER NOT NULL,source_id INTEGER NOT NULL,source_site TEXT NOT NULL,destination_site TEXT NOT NULL,context_origin TEXT NOT NULL,reporting_origin TEXT NOT NULL,reporting_site TEXT NOT NULL,time INTEGER NOT NULL,source_expiry_or_attribution_time INTEGER NOT NULL,report_id INTEGER NOT NULL,deactivated_for_source_destination_limit INTEGER NOT NULL,destination_limit_priority INTEGER NOT NULL);

CREATE TABLE dedup_keys(source_id INTEGER NOT NULL,report_type INTEGER NOT NULL,dedup_key INTEGER NOT NULL,PRIMARY KEY(source_id,report_type,dedup_key))WITHOUT ROWID;

CREATE TABLE source_destinations(source_id INTEGER NOT NULL,destination_site TEXT NOT NULL,PRIMARY KEY(source_id,destination_site))WITHOUT ROWID;

CREATE TABLE aggregatable_debug_rate_limits(id INTEGER PRIMARY KEY NOT NULL,context_site TEXT NOT NULL,reporting_origin TEXT NOT NULL,reporting_site TEXT NOT NULL,time INTEGER NOT NULL,consumed_budget INTEGER NOT NULL);

CREATE TABLE os_registrations(registration_origin TEXT NOT NULL,time INTEGER NOT NULL,PRIMARY KEY(registration_origin,time))WITHOUT ROWID;

CREATE TABLE meta(key LONGVARCHAR NOT NULL UNIQUE PRIMARY KEY, value LONGVARCHAR);

INSERT INTO meta VALUES('mmap_status','-1');
INSERT INTO meta VALUES('version','69');
INSERT INTO meta VALUES('last_compatible_version','69');

CREATE INDEX sources_by_active_reporting_origin ON sources(event_level_active,aggregatable_active,reporting_origin);

CREATE INDEX sources_by_expiry_time ON sources(expiry_time);

CREATE INDEX active_sources_by_source_origin ON sources(source_origin)WHERE event_level_active=1 OR aggregatable_active=1;

CREATE INDEX sources_by_source_time ON sources(source_time);

CREATE INDEX sources_by_destination_site ON source_destinations(destination_site);

CREATE INDEX reports_by_report_time ON reports(report_time);

CREATE INDEX reports_by_source_id_report_type ON reports(source_id,report_type);

CREATE INDEX reports_by_trigger_time ON reports(trigger_time);

CREATE INDEX reports_by_reporting_origin ON reports(reporting_origin)WHERE report_type=2;

CREATE INDEX reports_by_context_site ON reports(context_site)WHERE report_type=1;

CREATE INDEX rate_limit_reporting_origin_idx ON rate_limits(scope,source_site,destination_site);

CREATE INDEX rate_limit_time_idx ON rate_limits(time);

CREATE INDEX rate_limit_source_id_idx ON rate_limits(source_id);

CREATE INDEX rate_limit_report_id_idx ON rate_limits(scope,report_id)WHERE (scope=1 OR scope=2) AND report_id!=-1;

CREATE INDEX rate_limit_attribution_destination_reporting_site_idx ON rate_limits(scope,destination_site,reporting_site)WHERE(scope=1 OR scope=2);

CREATE INDEX rate_limit_source_reporting_site_idx ON rate_limits(reporting_site)WHERE scope=0;

CREATE INDEX aggregatable_debug_rate_limits_context_site_idx ON aggregatable_debug_rate_limits(context_site);

CREATE INDEX aggregatable_debug_rate_limits_time_idx ON aggregatable_debug_rate_limits(time);

CREATE INDEX os_registrations_time_idx ON os_registrations(time);

COMMIT;
