use django_erd_layout_core::{
    LayoutEdgeInput, LayoutNodeInput, LayoutOptimizerSettings, compute_bounds,
    optimize_locations_with_settings,
};
use std::sync::Mutex;

static OUTPUTS: Mutex<Vec<Option<Vec<u8>>>> = Mutex::new(Vec::new());

#[unsafe(no_mangle)]
pub extern "C" fn layout_wasm_alloc(len: usize) -> *mut u8 {
    let mut buffer = Vec::<u8>::with_capacity(len);
    let pointer = buffer.as_mut_ptr();
    std::mem::forget(buffer);
    pointer
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn layout_wasm_dealloc(pointer: *mut u8, len: usize) {
    if pointer.is_null() {
        return;
    }

    unsafe {
        drop(Vec::from_raw_parts(pointer, 0, len));
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn layout_wasm_optimize_locations(
    input_pointer: *const u8,
    input_len: usize,
) -> u64 {
    let Some(input) = (unsafe { read_input(input_pointer, input_len) }) else {
        return 0;
    };
    let Ok((settings, nodes, edges)) = decode_optimizer_input(input) else {
        return 0;
    };
    let optimized = optimize_locations_with_settings(&nodes, &edges, settings);
    let mut output = Vec::new();

    write_u32(&mut output, optimized.len() as u32);
    for node in optimized {
        write_string(&mut output, &node.id);
        write_f64(&mut output, node.x);
        write_f64(&mut output, node.y);
    }

    store_output(output)
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn layout_wasm_compute_bounds(
    input_pointer: *const u8,
    input_len: usize,
) -> u64 {
    let Some(input) = (unsafe { read_input(input_pointer, input_len) }) else {
        return 0;
    };
    let Ok(nodes) = decode_bounds_input(input) else {
        return 0;
    };
    let Some(bounds) = compute_bounds(&nodes) else {
        return 0;
    };
    let mut output = Vec::with_capacity(4 + 8 * 4);

    write_u32(&mut output, bounds.visible_count as u32);
    write_f64(&mut output, bounds.min_x);
    write_f64(&mut output, bounds.min_y);
    write_f64(&mut output, bounds.max_x);
    write_f64(&mut output, bounds.max_y);

    store_output(output)
}

#[unsafe(no_mangle)]
pub extern "C" fn layout_wasm_output_ptr(handle: u64) -> usize {
    with_output(handle, |output| output.as_ptr() as usize).unwrap_or(0)
}

#[unsafe(no_mangle)]
pub extern "C" fn layout_wasm_output_len(handle: u64) -> usize {
    with_output(handle, Vec::len).unwrap_or(0)
}

#[unsafe(no_mangle)]
pub extern "C" fn layout_wasm_free_output(handle: u64) {
    if handle == 0 {
        return;
    }

    let index = (handle - 1) as usize;
    let Ok(mut outputs) = OUTPUTS.lock() else {
        return;
    };

    if let Some(slot) = outputs.get_mut(index) {
        *slot = None;
    }
}

unsafe fn read_input<'a>(input_pointer: *const u8, input_len: usize) -> Option<&'a [u8]> {
    if input_pointer.is_null() {
        return None;
    }

    Some(unsafe { std::slice::from_raw_parts(input_pointer, input_len) })
}

fn decode_optimizer_input(
    input: &[u8],
) -> Result<
    (
        LayoutOptimizerSettings,
        Vec<LayoutNodeInput>,
        Vec<LayoutEdgeInput>,
    ),
    (),
> {
    let mut cursor = Cursor::new(input);
    let node_count = cursor.read_u32()? as usize;
    let edge_count = cursor.read_u32()? as usize;
    let settings = LayoutOptimizerSettings {
        node_spacing: cursor.read_f64()?,
        edge_detour: cursor.read_f64()?,
    };
    let mut nodes = Vec::with_capacity(node_count);
    let mut edges = Vec::with_capacity(edge_count);

    for _ in 0..node_count {
        nodes.push(cursor.read_node()?);
    }

    for _ in 0..edge_count {
        edges.push(LayoutEdgeInput {
            source_id: cursor.read_string()?,
            target_id: cursor.read_string()?,
        });
    }

    Ok((settings, nodes, edges))
}

fn decode_bounds_input(input: &[u8]) -> Result<Vec<LayoutNodeInput>, ()> {
    let mut cursor = Cursor::new(input);
    let node_count = cursor.read_u32()? as usize;
    let mut nodes = Vec::with_capacity(node_count);

    for _ in 0..node_count {
        nodes.push(cursor.read_node()?);
    }

    Ok(nodes)
}

fn store_output(output: Vec<u8>) -> u64 {
    let Ok(mut outputs) = OUTPUTS.lock() else {
        return 0;
    };

    for (index, slot) in outputs.iter_mut().enumerate() {
        if slot.is_none() {
            *slot = Some(output);
            return (index + 1) as u64;
        }
    }

    outputs.push(Some(output));
    outputs.len() as u64
}

fn with_output<T>(handle: u64, map: impl FnOnce(&Vec<u8>) -> T) -> Option<T> {
    if handle == 0 {
        return None;
    }

    let index = (handle - 1) as usize;
    let Ok(outputs) = OUTPUTS.lock() else {
        return None;
    };

    outputs.get(index)?.as_ref().map(map)
}

fn write_u32(output: &mut Vec<u8>, value: u32) {
    output.extend_from_slice(&value.to_le_bytes());
}

fn write_f64(output: &mut Vec<u8>, value: f64) {
    output.extend_from_slice(&value.to_le_bytes());
}

fn write_string(output: &mut Vec<u8>, value: &str) {
    write_u32(output, value.len() as u32);
    output.extend_from_slice(value.as_bytes());
}

struct Cursor<'a> {
    input: &'a [u8],
    offset: usize,
}

impl<'a> Cursor<'a> {
    fn new(input: &'a [u8]) -> Self {
        Self { input, offset: 0 }
    }

    fn read_node(&mut self) -> Result<LayoutNodeInput, ()> {
        Ok(LayoutNodeInput {
            id: self.read_string()?,
            x: self.read_f64()?,
            y: self.read_f64()?,
            width: self.read_f64()?,
            height: self.read_f64()?,
        })
    }

    fn read_string(&mut self) -> Result<String, ()> {
        let len = self.read_u32()? as usize;
        let bytes = self.read_bytes(len)?;
        String::from_utf8(bytes.to_vec()).map_err(|_| ())
    }

    fn read_u32(&mut self) -> Result<u32, ()> {
        let bytes = self.read_bytes(4)?;
        let mut value = [0u8; 4];
        value.copy_from_slice(bytes);
        Ok(u32::from_le_bytes(value))
    }

    fn read_f64(&mut self) -> Result<f64, ()> {
        let bytes = self.read_bytes(8)?;
        let mut value = [0u8; 8];
        value.copy_from_slice(bytes);
        Ok(f64::from_le_bytes(value))
    }

    fn read_bytes(&mut self, len: usize) -> Result<&'a [u8], ()> {
        let end = self.offset.checked_add(len).ok_or(())?;
        if end > self.input.len() {
            return Err(());
        }

        let bytes = &self.input[self.offset..end];
        self.offset = end;
        Ok(bytes)
    }
}
