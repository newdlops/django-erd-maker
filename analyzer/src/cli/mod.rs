use crate::extract::{AnalysisRequest, ModuleInput, analyze_request, analyze_request_with_metrics};
use crate::layout_engine::{LayoutRequest, compute_layout};
use crate::protocol::hello::HelloPayload;
use crate::protocol::layout::LayoutMode;
use crate::protocol::webview::{
    DiagramBootstrapPayload, InitialViewState, PipelineTimings, TableViewOptions,
};
use crate::resolve::build_diagram_graph;
use serde::Deserialize;
use std::env;
use std::fs;
use std::path::PathBuf;
use std::process;
use std::time::Instant;

pub fn run() {
    let arguments = env::args().skip(1).collect::<Vec<_>>();
    if !arguments.is_empty() {
        match arguments[0].as_str() {
            "analyze" => match parse_analysis_request(&arguments[1..]) {
                Ok(request) => {
                    println!("{}", analyze_request(&request).to_json());
                    return;
                }
                Err(message) => exit_with_error(&message),
            },
            "graph" => match parse_analysis_request(&arguments[1..]) {
                Ok(request) => {
                    let analyzer = analyze_request(&request);
                    println!("{}", build_diagram_graph(&analyzer).to_json());
                    return;
                }
                Err(message) => exit_with_error(&message),
            },
            "layout" => match parse_layout_command(&arguments[1..]) {
                Ok(command) => {
                    let analyzer = analyze_request(&command.request);
                    let graph = build_diagram_graph(&analyzer);
                    let layout = compute_layout(LayoutRequest {
                        analyzer: &analyzer,
                        graph: &graph,
                        hidden_model_ids: Vec::new(),
                        manual_positions: Vec::new(),
                        mode: command.mode,
                    });
                    println!("{}", layout.to_json());
                    return;
                }
                Err(message) => exit_with_error(&message),
            },
            "bootstrap" => match parse_layout_command(&arguments[1..]) {
                Ok(command) => {
                    let bootstrap_started = Instant::now();
                    let (analyzer, analysis_metrics) =
                        analyze_request_with_metrics(&command.request);
                    let graph_started = Instant::now();
                    let graph = build_diagram_graph(&analyzer);
                    let graph_ms = elapsed_ms(graph_started);
                    let layout_started = Instant::now();
                    let layout = compute_layout(LayoutRequest {
                        analyzer: &analyzer,
                        graph: &graph,
                        hidden_model_ids: Vec::new(),
                        manual_positions: Vec::new(),
                        mode: command.mode,
                    });
                    let mut payload = DiagramBootstrapPayload::new(
                        analyzer,
                        graph.clone(),
                        layout,
                        initial_view_state(command.mode, &graph),
                    );
                    payload.timings = Some(PipelineTimings {
                        analyzer_bootstrap_ms: Some(elapsed_ms(bootstrap_started)),
                        discovery_ms: None,
                        extract_ms: Some(analysis_metrics.extract_ms),
                        graph_ms: Some(graph_ms),
                        layout_ms: Some(elapsed_ms(layout_started)),
                        parse_ms: Some(analysis_metrics.parse_ms),
                        render_document_ms: None,
                    });
                    println!("{}", payload.to_json());
                    return;
                }
                Err(message) => exit_with_error(&message),
            },
            unknown => exit_with_error(&format!(
                "unsupported command '{unknown}'; expected 'analyze', 'graph', 'layout', or 'bootstrap'"
            )),
        }
    }

    let payload = HelloPayload::new(
        "django-erd-maker analyzer scaffold",
        env!("CARGO_PKG_VERSION"),
    );

    println!("{}", payload.to_json());
}

fn parse_analysis_request(arguments: &[String]) -> Result<AnalysisRequest, String> {
    let mut workspace_root =
        env::current_dir().map_err(|error| format!("failed to read current directory: {error}"))?;
    let mut modules = Vec::new();
    let mut index = 0;

    while index < arguments.len() {
        match arguments[index].as_str() {
            "--workspace-root" => {
                let value = arguments
                    .get(index + 1)
                    .ok_or_else(|| "--workspace-root requires a path".to_string())?;
                workspace_root = PathBuf::from(value);
                index += 2;
            }
            "--module" => {
                let value = arguments
                    .get(index + 1)
                    .ok_or_else(|| "--module requires APP_LABEL=PATH".to_string())?;
                modules.push(parse_module_argument(value)?);
                index += 2;
            }
            "--request-file" => {
                let value = arguments
                    .get(index + 1)
                    .ok_or_else(|| "--request-file requires a path".to_string())?;
                let request = read_analysis_request_file(value)?;
                workspace_root = request.workspace_root;
                modules.extend(request.modules);
                index += 2;
            }
            unknown => return Err(format!("unknown analyzer argument '{unknown}'")),
        }
    }

    if modules.is_empty() {
        return Err("analyze requires at least one --module APP_LABEL=PATH".to_string());
    }

    Ok(AnalysisRequest {
        modules,
        workspace_root,
    })
}

#[derive(Debug, Deserialize)]
#[serde(rename_all = "camelCase")]
struct AnalysisRequestFile {
    modules: Vec<AnalysisRequestFileModule>,
    workspace_root: PathBuf,
}

#[derive(Debug, Deserialize)]
#[serde(rename_all = "camelCase")]
struct AnalysisRequestFileModule {
    app_label: String,
    file_path: PathBuf,
}

fn read_analysis_request_file(file_path: &str) -> Result<AnalysisRequest, String> {
    let source = fs::read_to_string(file_path)
        .map_err(|error| format!("failed to read analyzer request file '{file_path}': {error}"))?;
    let request = serde_json::from_str::<AnalysisRequestFile>(&source)
        .map_err(|error| format!("failed to parse analyzer request file '{file_path}': {error}"))?;

    Ok(AnalysisRequest {
        modules: request
            .modules
            .into_iter()
            .map(|module| ModuleInput {
                app_label: module.app_label,
                file_path: module.file_path,
            })
            .collect(),
        workspace_root: request.workspace_root,
    })
}

struct LayoutCommand {
    mode: LayoutMode,
    request: AnalysisRequest,
}

fn parse_layout_command(arguments: &[String]) -> Result<LayoutCommand, String> {
    if arguments.len() < 2 || arguments[0] != "--mode" {
        return Err("layout requires --mode <hierarchical|circular|clustered>".to_string());
    }

    let mode = parse_layout_mode(&arguments[1])?;
    let request = parse_analysis_request(&arguments[2..])?;

    Ok(LayoutCommand { mode, request })
}

fn parse_module_argument(value: &str) -> Result<ModuleInput, String> {
    let (app_label, file_path) = value
        .split_once('=')
        .ok_or_else(|| format!("invalid module argument '{value}'; expected APP_LABEL=PATH"))?;

    if app_label.is_empty() || file_path.is_empty() {
        return Err(format!(
            "invalid module argument '{value}'; both app label and path are required"
        ));
    }

    Ok(ModuleInput {
        app_label: app_label.to_string(),
        file_path: PathBuf::from(file_path),
    })
}

fn parse_layout_mode(value: &str) -> Result<LayoutMode, String> {
    match value {
        "circular" => Ok(LayoutMode::Circular),
        "clustered" => Ok(LayoutMode::Clustered),
        "hierarchical" => Ok(LayoutMode::Hierarchical),
        _ => Err(format!(
            "unsupported layout mode '{value}'; expected hierarchical, circular, or clustered"
        )),
    }
}

fn initial_view_state(
    mode: LayoutMode,
    graph: &crate::protocol::graph::DiagramGraph,
) -> InitialViewState {
    let mut table_options = graph
        .nodes
        .iter()
        .map(|node| TableViewOptions {
            hidden: false,
            manual_position: None,
            model_id: node.model_id.clone(),
            show_method_highlights: true,
            show_methods: true,
            show_properties: true,
        })
        .collect::<Vec<_>>();
    table_options.sort_by(|left, right| left.model_id.as_str().cmp(right.model_id.as_str()));

    InitialViewState {
        layout_mode: mode,
        selected_method_context: None,
        selected_model_id: graph.nodes.first().map(|node| node.model_id.clone()),
        table_options,
    }
}

fn elapsed_ms(started: Instant) -> f64 {
    started.elapsed().as_secs_f64() * 1000.0
}

fn exit_with_error(message: &str) -> ! {
    eprintln!("{message}");
    process::exit(1);
}
