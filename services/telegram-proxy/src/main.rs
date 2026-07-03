use axum::{
    Json, Router,
    extract::State,
    http::StatusCode,
    routing::{get, post},
};
use reqwest::Client;
use serde::Deserialize;

const HOST: &str = "0.0.0.0";
const PORT: u16 = 3333;

#[derive(Clone)]
struct AppState {
    http_client: Client,
    telegram_bot_token: String,
    telegram_chat_id: String,
}

#[derive(Debug, Deserialize)]
struct EventRequest {
    what: String,
    when: String,
    who: String,

    #[serde(rename = "where")]
    where_: String,
}

#[tokio::main]
async fn main() {
    tracing_subscriber::fmt::init();
    tracing::info!("starting Telegram proxy server");

    dotenvy::dotenv().ok();

    let state = AppState {
        http_client: Client::builder()
            .timeout(std::time::Duration::from_secs(10))
            .build()
            .expect("failed to build HTTP client"),
        telegram_bot_token: std::env::var("TELEGRAM_BOT_TOKEN")
            .expect("TELEGRAM_BOT_TOKEN must be set"),
        telegram_chat_id: std::env::var("TELEGRAM_CHAT_ID").expect("TELEGRAM_CHAT_ID must be set"),
    };

    let app = Router::new()
        .route("/events", post(events))
        .route("/health", get(health))
        .with_state(state);

    let addr = format!("{HOST}:{PORT}");
    let listener = tokio::net::TcpListener::bind(&addr).await.unwrap();

    tracing::info!("listening on http://{addr}");
    axum::serve(listener, app).await.unwrap();
}

async fn events(State(state): State<AppState>, Json(event): Json<EventRequest>) -> StatusCode {
    tracing::info!(?event, "received /events request");

    let text = format!(
        "{} - {}: {} in {}",
        event.when, event.who, event.what, event.where_
    );

    match send_telegram_message(
        &state.http_client,
        &state.telegram_bot_token,
        &state.telegram_chat_id,
        &text,
    )
    .await
    {
        Ok(()) => StatusCode::NO_CONTENT,
        Err(err) => {
            tracing::error!(error = %err, "Telegram request error");
            StatusCode::BAD_GATEWAY
        }
    }
}

async fn health() -> StatusCode {
    tracing::info!("received /health request");
    StatusCode::NO_CONTENT
}

async fn send_telegram_message(
    http_client: &Client,
    token: &str,
    chat_id: &str,
    message: &str,
) -> Result<(), reqwest::Error> {
    tracing::info!(message = %message, "sending Telegram message");
    let url = format!("https://api.telegram.org/bot{token}/sendMessage");

    let response = http_client
        .post(url)
        .form(&[("chat_id", chat_id), ("text", message)])
        .send()
        .await?
        .error_for_status()?;

    tracing::info!(status = %response.status(), "sent Telegram message");

    Ok(())
}
